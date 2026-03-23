/**
 * @file   video_rxtx/omt.cpp
 * @author Martin Piatka     <piatka@cesnet.cz>
 */
/*
 * Copyright (c) 2026 CESNET, zájmové sdružení právických osob
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

#include <cassert>
#include <config.h>
#include <libomt.h>

#include "debug.h"
#include "lib_common.h"
#include "video_rxtx.hpp"
#include "video_codec.h"
#include "video_display.h"
#include "video_frame.h"

#define MOD_NAME "[OMT] "

namespace{
void omt_log_callback(const char *msg){
        log_msg(LOG_LEVEL_INFO, MOD_NAME "OMTLOG: %s\n", msg);
}

struct omt_rxtx_state{
        module *parent = nullptr;
        omt_receive_t *omt_recv_handle = nullptr;
        omt_send_t *omt_send_handle = nullptr;

        OMTMediaFrame send_video_frame{};

        std::atomic<bool> should_exit = false;

        video_desc send_desc{};
        video_desc recv_desc{};
        display *display_device = nullptr;
};

void omt_should_exit_callback(void *state){
        auto s = static_cast<omt_rxtx_state *>(state);
        s->should_exit = true;
}

void *omt_rxtx_create(const vrxtx_params *params, const common_opts *common){
        auto s = new omt_rxtx_state();

        s->parent = common->parent;
        omt_setloggingcallback(omt_log_callback);
        s->display_device = params->display_device;
        log_msg(LOG_LEVEL_INFO, MOD_NAME "Create omt receive with address %s\n", params->receiver);
        s->omt_recv_handle = omt_receive_create(params->receiver, static_cast<OMTFrameType>(OMTFrameType_Audio | OMTFrameType_Video),
                OMTPreferredVideoFormat_UYVY, OMTReceiveFlags_None);
        s->omt_send_handle = omt_send_create("UltraGrid", OMTQuality_Default);

        OMTSenderInfo info = {};
        std::string productName = "UltraGrid";
        std::string manufacturer = "CESNET";
        std::string version = VERSION;
        productName.copy(info.ProductName, OMT_MAX_STRING_LENGTH, 0);
        manufacturer.copy(info.Manufacturer, OMT_MAX_STRING_LENGTH, 0);
        version.copy(info.Version, OMT_MAX_STRING_LENGTH, 0);
        omt_send_setsenderinformation(s->omt_send_handle, &info);
        s->send_video_frame.Type = OMTFrameType_Video;
        s->send_video_frame.Timestamp = -1;

        return s;
}

void omt_rxtx_done(void *state){
        auto s = static_cast<omt_rxtx_state *>(state);
        delete s;
}

void omt_rxtx_send_frame(void *state, std::shared_ptr<video_frame> f){
        auto s = static_cast<omt_rxtx_state *>(state);
        auto frame_desc = video_desc_from_frame(f.get());
        if(!video_desc_eq(s->send_desc, frame_desc)){
                log_msg(LOG_LEVEL_NOTICE, MOD_NAME "Reconf\n");
                s->send_desc = frame_desc;
                s->send_video_frame.Width = frame_desc.width;
                s->send_video_frame.Height = frame_desc.height;
                if(frame_desc.color_spec != UYVY){
                        return;
                }
                s->send_video_frame.Codec = OMTCodec_UYVY;
                s->send_video_frame.ColorSpace = OMTColorSpace_BT709;
                s->send_video_frame.FrameRateN = frame_desc.fps * 1000.0;
                s->send_video_frame.FrameRateD = 1000;
        }

        s->send_video_frame.Stride = vc_get_linesize(f->tiles[0].width, f->color_spec);
        s->send_video_frame.Data = f->tiles[0].data;
        s->send_video_frame.DataLength = f->tiles[0].data_len;

        int ret = omt_send(s->omt_send_handle, &s->send_video_frame);

        log_msg(LOG_LEVEL_INFO, MOD_NAME "Send returned %d\n", ret);
}

void *omt_rxtx_recv_worker(void *state){
        auto s = static_cast<omt_rxtx_state *>(state);
        register_should_exit_callback(s->parent,
                                      omt_should_exit_callback, state);

        while(!s->should_exit){
                auto omt_frame = omt_receive(s->omt_recv_handle, OMTFrameType_Video, 1000);
                if(!omt_frame){
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "Receive failed\n");
                        continue;
                }

                video_desc incoming_desc{};
                incoming_desc.fps = static_cast<double>(omt_frame->FrameRateN) / omt_frame->FrameRateD;
                incoming_desc.width = omt_frame->Width;
                incoming_desc.height = omt_frame->Height;
                incoming_desc.color_spec = UYVY; assert(omt_frame->Codec == OMTCodec_UYVY); //TODO
                incoming_desc.tile_count = 1;

                if(!video_desc_eq(incoming_desc, s->recv_desc)){
                        display_reconfigure(s->display_device, incoming_desc, VIDEO_NORMAL);
                        s->recv_desc = incoming_desc;
                }

                auto ug_frame = display_get_frame(s->display_device);
                assert(omt_frame->Stride == vc_get_linesize(ug_frame->tiles[0].width, ug_frame->color_spec));
                memcpy(ug_frame->tiles[0].data, omt_frame->Data, omt_frame->DataLength);

                display_put_frame(s->display_device, ug_frame, PUTF_BLOCKING);
        }

        display_put_frame(s->display_device, nullptr, PUTF_BLOCKING);
        unregister_should_exit_callback(s->parent,
                                        omt_should_exit_callback, s);

        return nullptr;
}
}

constexpr video_rxtx_info loopback_video_rxtx_info = {
        "Open media transport",
        omt_rxtx_create,
        omt_rxtx_done,
        omt_rxtx_send_frame,
        nullptr,
        nullptr,
        nullptr,
        omt_rxtx_recv_worker
};


REGISTER_MODULE(omt, &loopback_video_rxtx_info, LIBRARY_CLASS_VIDEO_RXTX, VIDEO_RXTX_ABI_VERSION);
