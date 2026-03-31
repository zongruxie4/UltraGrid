/**
 * @file   video_rxtx.cpp
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2013-2026 CESNET, zájmové sdružení právnických osob
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cassert>           // for assert
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>

#include "debug.h"
#include "export.h"
#include "host.h"
#include "lib_common.h"
#include "messaging.h"
#include "module.h"
#include "pdb.h"
#include "rtp/rtp.h"
#include "rtp/video_decoders.h"
#include "rtp/pbuf.h"
#include "tfrc.h"
#include "transmit.h"
#include "tv.h"
#include "utils/thread.h"
#include "utils/vf_split.h"
#include "video.h"
#include "video_compress.h"
#include "video_decompress.h"
#include "video_display.h"
#include "video_rxtx.hpp"

constexpr char DEFAULT_VIDEO_COMPRESSION[] = "none";

#define MOD_NAME "[vrxtx] "

using std::map;
using std::shared_ptr;
using std::ostringstream;
using std::string;

const char *
vrxtx_get_compression(const char *video_protocol, const char *req_compression)
{
        if (req_compression != nullptr) {
                return req_compression;
        }
        // default values for different RXTX protocols
        if (strcasecmp(video_protocol, "rtsp") == 0 ||
            strcasecmp(video_protocol, "sdp") == 0) {
                return "none"; // will be set later by video_rxtx::send_frame()
        }
        // UG RTP or loopback
        return DEFAULT_VIDEO_COMPRESSION;
}

video_rxtx::video_rxtx(const char                *protocol_name,
                       const struct vrxtx_params *params,
                       const struct common_opts  *common) noexcept(false)
    : m_exporter(common->exporter)
{
        module_init_default(&m_sender_mod);
        m_sender_mod.cls = MODULE_CLASS_SENDER;
        module_register(&m_sender_mod, common->parent);

        const char *compression =
            vrxtx_get_compression(protocol_name, params->compression);
        int ret = compress_init(&m_sender_mod, compression, &m_compression);
        if(ret != 0) {
                module_done(&m_sender_mod);
                if(ret < 0) {
                        throw string("Error initializing compression.");
                }
                if(ret > 0) {
                        throw EXIT_SUCCESS;
                }
        }

        module_init_default(&m_receiver_mod);
        m_receiver_mod.cls = MODULE_CLASS_RECEIVER;
        module_register(&m_receiver_mod, common->parent);

        pthread_mutex_init(&m_lock, nullptr);
}

video_rxtx::~video_rxtx() noexcept
{
        if (!pthread_equal(m_receiver_thread_id, PTHREAD_NULL)) {
                pthread_join(m_receiver_thread_id, nullptr);
        }

        join();
        if (!m_sender_poisoned && m_compression != nullptr) {
                send(NULL);
                compress_pop(m_compression);
        }
        if (m_impl_funcs != nullptr && m_impl_state != nullptr) {
                m_impl_funcs->done(m_impl_state);
        }
        compress_done(m_compression);
        module_done(&m_receiver_mod);
        module_done(&m_sender_mod);

        pthread_mutex_destroy(&m_lock);
}

void
video_rxtx::join() noexcept
{
        if (!pthread_equal(m_receiver_thread_id, PTHREAD_NULL)) {
                pthread_join(m_receiver_thread_id, nullptr);
                m_receiver_thread_id = PTHREAD_NULL;
        }

        if (m_sender_joined) {
                return;
        }
        send(NULL); // pass poisoned pill
        pthread_join(m_sender_thread_id, NULL);
        if (m_impl_funcs != nullptr && m_impl_funcs->join_sender != nullptr) {
                m_impl_funcs->join_sender(m_impl_state);
        }
        m_sender_joined = true;
}

const char *
video_rxtx::get_long_name(string const &short_name) noexcept
{
        const auto *vri = static_cast<const video_rxtx_info *>(
            load_library(short_name.c_str(), LIBRARY_CLASS_VIDEO_RXTX,
                         VIDEO_RXTX_ABI_VERSION));

        if (vri != nullptr) {
                return vri->long_name;
        }
        return "(ERROR)";
}

void
video_rxtx::send(shared_ptr<video_frame> frame) noexcept
{
        if (!frame && m_sender_poisoned) {
                return;
        }
        if (!frame) {
                m_sender_poisoned = true;
        } else {
                m_input_codec = frame->color_spec;
        }
        compress_frame(m_compression, std::move(frame));
}

void *video_rxtx::sender_thread(void *args) {
        return static_cast<video_rxtx *>(args)->sender_loop();
}

void video_rxtx::check_sender_messages() {
        // process external messages
        struct message *msg_external = nullptr;
        while((msg_external = check_message(&m_sender_mod))) {
                struct response *r = nullptr;
                auto *msg = (struct msg_sender *) msg_external;
                if (msg->type == SENDER_MSG_QUERY_VIDEO_MODE) {
                        if (!m_video_desc) {
                                r = new_response(RESPONSE_NO_CONTENT, nullptr);
                        } else {
                                ostringstream oss;
                                oss << m_video_desc
                                    << " (input " << get_codec_name(m_input_codec)
                                    << ")";
                                r = new_response(RESPONSE_OK,
                                                 oss.str().c_str());
                        }
                } else {
                        char buf[200];
                        snprintf_ch(buf, "Unexpected sender message type %d",
                                    msg->type);
                        MSG(WARNING, "%s\n", buf);
                        r = new_response(RESPONSE_BAD_REQUEST, buf);
                }

                free_message(msg_external, r);
        }
}

void *video_rxtx::sender_loop() {
        set_thread_name(__func__);
        struct video_desc saved_vid_desc;

        memset(&saved_vid_desc, 0, sizeof(saved_vid_desc));

        while(1) {
                check_sender_messages();

                shared_ptr<video_frame> tx_frame;

                tx_frame = compress_pop(m_compression);
                if (!tx_frame) {
                        break;
                }

                m_video_desc = video_desc_from_frame(tx_frame.get());
                export_video(m_exporter, tx_frame.get());

                m_impl_funcs->send_frame(m_impl_state, std::move(tx_frame));
                m_frames_sent += 1;
        }

        check_sender_messages();
        return NULL;
}

/**
 * @retval nullptr    if video_rxtx initialization failed
 * @retval INIT_NOERR if help requested and shown (no state created,
                      beware not to delete the INIT_NOERR ptr)
 * @retval other      the vrxtx state
 */
video_rxtx *
video_rxtx::create(string const &proto, const struct vrxtx_params *params,
                   const struct common_opts *common) noexcept
{
        auto vri = static_cast<const video_rxtx_info *>(load_library(proto.c_str(), LIBRARY_CLASS_VIDEO_RXTX, VIDEO_RXTX_ABI_VERSION));
        if (!vri) {
                if (proto != "help") {
                        error_msg("Requested RX/TX cannot be created "
                                  "(missing library?)\n");
                        return nullptr;
                }
                return (video_rxtx *) INIT_NOERR;
        }

        video_rxtx *ret = nullptr;
        try {
                ret = new video_rxtx(proto.c_str(), params, common);
        } catch (const string &s) { // video compress init faillure
                MSG(ERROR, "%s\n", s.c_str());
                return nullptr;
        } catch (...) { // video compress init help
                return (video_rxtx *) INIT_NOERR;
        }

        auto params_c = *params;
        params_c.sender_mod  = &ret->m_sender_mod;
        params_c.receiver_mod  = &ret->m_receiver_mod;
        try {
                ret->m_impl_state = vri->create(&params_c, common);
        } catch (...) {
        }
        ret->m_impl_funcs = vri;
        if (ret->m_impl_state == nullptr) {
                delete ret;
                if (strcmp(params->protocol_opts, "help") == 0) {
                        return (video_rxtx *) INIT_NOERR;
                }
                return nullptr;
        }

        if ((params->rxtx_mode & MODE_RECEIVER) != 0U) {
                if (ret->m_impl_funcs->receiver_routine == nullptr) {
                        MSG(ERROR,
                            "Selected RX/TX mode doesn't support receiving.\n");
                        delete ret;
                        return nullptr;
                }
                int rc = pthread_create(&ret->m_receiver_thread_id, nullptr,
                                        ret->m_impl_funcs->receiver_routine,
                                        ret->m_impl_state);
                assert(rc == 0);
        }

        int rc = pthread_create(&ret->m_sender_thread_id, nullptr, video_rxtx::sender_thread,
                           (void *) ret);
        assert(rc == 0);
        ret->m_sender_joined = false;

        return ret;
}

void
video_rxtx::list(bool full) noexcept
{
        printf("Available TX protocols:\n");
        list_modules(LIBRARY_CLASS_VIDEO_RXTX, VIDEO_RXTX_ABI_VERSION, full);
}

void
video_rxtx::set_audio_spec(const struct audio_desc * desc,
                           int audio_rx_port, int audio_tx_port,
                           bool ipv6) noexcept
{
        if (m_impl_funcs->set_sender_audio_spec != nullptr) {
                m_impl_funcs->set_sender_audio_spec(m_impl_state, desc, audio_rx_port,
                                             audio_tx_port, ipv6);
        } else {
                MSG(INFO, "video RXTX not h264_rtp, not setting audio...\n");
        }
}
