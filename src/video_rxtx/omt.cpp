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
#include "omt.hpp"

#include "debug.h"
#include "lib_common.h"
#include "video_codec.h"
#include "video_display.h"
#include "video_frame.h"

#define MOD_NAME "[OMT] "

omt_video_rxtx::omt_video_rxtx(std::map<std::string, param_u> const& params) : video_rxtx(params){
        display_device = static_cast<display *>(params.at("display_device").ptr);
        omt_recv_handle = omt_receive_create("omt://192.168.2.86:6400", static_cast<OMTFrameType>(OMTFrameType_Audio | OMTFrameType_Video),
                OMTPreferredVideoFormat_UYVY, OMTReceiveFlags_None);
        omt_send_handle = omt_send_create("UltraGrid", OMTQuality_Default);

        send_video_frame.Type = OMTFrameType_Video;
        send_video_frame.Timestamp = -1;
}

omt_video_rxtx::~omt_video_rxtx()= default;

void *omt_video_rxtx::recv_worker(void *arg){
        auto s = static_cast<omt_video_rxtx *>(arg);
        bool should_exit = false;

        while(!should_exit){
                auto omt_frame = omt_receive(s->omt_recv_handle, OMTFrameType_Video, 1000);
                if(!omt_frame){
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "Receive failed\n");
                        continue;
                }

                video_desc incoming_desc{};
                log_msg(LOG_LEVEL_INFO, "FRAME\n");
                log_msg(LOG_LEVEL_NOTICE, MOD_NAME "Received a video frame %dx%d %d\n", omt_frame->Width, omt_frame->Height, omt_frame->Codec);
                incoming_desc.fps = static_cast<double>(omt_frame->FrameRateN) / omt_frame->FrameRateD;
                incoming_desc.width = omt_frame->Width;
                incoming_desc.height = omt_frame->Height;
                incoming_desc.color_spec = UYVY; assert(omt_frame->Codec == OMTCodec_UYVY); //TODO
                incoming_desc.tile_count = 1;

                auto ug_frame = vf_alloc_desc(incoming_desc);

                display_put_frame(s->display_device, ug_frame, 0);

                vf_free(ug_frame);
        }

        return nullptr;
}

void omt_video_rxtx::send_frame(std::shared_ptr<video_frame> f) noexcept{
        auto frame_desc = video_desc_from_frame(f.get());
        if(!video_desc_eq(desc, frame_desc)){
                log_msg(LOG_LEVEL_NOTICE, MOD_NAME "Reconf\n");
                desc = frame_desc;
                send_video_frame.Width = frame_desc.width;
                send_video_frame.Height = frame_desc.height;
                if(frame_desc.color_spec != UYVY){
                        return;
                }
                send_video_frame.Codec = OMTCodec_UYVY;
                send_video_frame.ColorSpace = OMTColorSpace_BT709;
                send_video_frame.FrameRateN = frame_desc.fps * 1000.0;
                send_video_frame.FrameRateD = 1000;
        }

        send_video_frame.Stride = vc_get_linesize(f->tiles[0].width, f->color_spec);
        send_video_frame.Data = f->tiles[0].data;
        send_video_frame.DataLength = f->tiles[0].data_len;

        int ret = omt_send(omt_send_handle, &send_video_frame);

        log_msg(LOG_LEVEL_INFO, MOD_NAME "Send returned %d\n", ret);
}

void *(*omt_video_rxtx::get_receiver_thread() noexcept)(void *arg){
        return recv_worker;
}

namespace{
video_rxtx *create_video_rxtx_omt(std::map<std::string, param_u> const& params){
        return new omt_video_rxtx(params);
}

const video_rxtx_info loopback_video_rxtx_info = {
        "Open media transport",
        create_video_rxtx_omt
};
}


REGISTER_MODULE(omt, &loopback_video_rxtx_info, LIBRARY_CLASS_VIDEO_RXTX, VIDEO_RXTX_ABI_VERSION);
