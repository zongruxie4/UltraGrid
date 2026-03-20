/**
 * @file   video_rxtx.hpp
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2013-2026 CESNET zájmové sdružení právnických osob
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

#ifndef VIDEO_RXTX_H_
#define VIDEO_RXTX_H_

#include <atomic>
#include <memory>
#include <string>

#include "host.h"
#include "module.h"
#include "types.h"    // for codec_t, video_desc, video_frame (ptr only)

#define VIDEO_RXTX_ABI_VERSION 4

struct audio_desc;
struct display;
struct module;
struct video_compress;
struct exporter;
struct video_frame;

struct vrxtx_params {
        const char           *compression;
        unsigned int          rxtx_mode;
        struct display       *display_device; // iHDTV, UG RTP
        struct vidcap        *capture_device; // iHDTV
        const char           *receiver;
        int                   rx_port;
        int                   tx_port;
        const char           *fec;
        long long             bitrate;
        enum video_mode       decoder_mode;
        const char           *protocol_opts;
        long long             start_time;
        long                  av_type;       // RTSP
        struct module        *sender_mod;
        struct module        *receiver_mod;
};

#define VRXTX_INIT \
        { \
                .compression    = nullptr, \
                .rxtx_mode      = 0, \
                .display_device = nullptr, \
                .capture_device = nullptr, \
                .receiver       = nullptr, \
                .rx_port        = -1, \
                .tx_port        = -1, \
                .fec            = "none", \
                .bitrate        = RATE_DEFAULT, \
                .decoder_mode   = VIDEO_NORMAL, \
                .protocol_opts  = "", \
                .start_time     = 0, \
                .av_type        = 0, \
                .sender_mod     = nullptr, \
                .receiver_mod   = nullptr, \
        }

struct video_rxtx_info {
        const char *long_name;
        void *(*create)(const struct vrxtx_params *params,
                        const struct common_opts  *common);
        void (*done)(void *state);
        void (*send_frame)(void *state, std::shared_ptr<video_frame>);

        // following callbacks are optional
        void (*join_sender)(void *state);
        void (*set_sender_audio_spec)(void                    *state,
                                      const struct audio_desc *desc,
                                      int audio_rx_port, int audio_tx_port,
                                      bool ipv6);
        struct response *(*process_sender_message)(void *state,
                                                   struct msg_sender *);
        void *(*receiver_routine)(void *state);
};

struct video_rxtx {
public:
        virtual ~video_rxtx();
        void send(std::shared_ptr<struct video_frame>);
        static const char *get_long_name(std::string const & short_name);
        static void *receiver_thread(void *arg) {
                video_rxtx *rxtx = static_cast<video_rxtx *>(arg);
                return rxtx->m_impl_funcs->receiver_routine(rxtx->m_impl_state);
        }
        bool supports_receiving() {
                return m_impl_funcs->receiver_routine != nullptr;
        }
        /**
         * If overridden, children must call also video_rxtx::join()
         */
        virtual void join();
        static video_rxtx   *create(std::string const         &name,
                                    const struct vrxtx_params *params,
                                    const struct common_opts  *opts);
        static void list(bool full);
        void set_audio_spec(const struct audio_desc *desc, int audio_rx_port,
                            int audio_tx_port, bool ipv6);
        std::string m_port_id;

        const struct video_rxtx_info *m_impl_funcs;
        void                         *m_impl_state;

protected:
        video_rxtx(const struct vrxtx_params *params,
                   const struct common_opts *opts);
        void check_sender_messages();
        struct module m_sender_mod;
        struct module m_receiver_mod;
        unsigned long long int m_frames_sent;
        struct exporter *m_exporter;

private:
        void start();
        static void *sender_thread(void *args);
        void *sender_loop();

        struct compress_state *m_compression = nullptr;
        pthread_mutex_t m_lock;

        pthread_t m_thread_id;
        bool m_poisoned, m_joined;

        video_desc       m_video_desc{};
        std::atomic<codec_t> m_input_codec{};
};

#endif // VIDEO_RXTX_H_

