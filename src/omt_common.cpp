/**
 * @file   omt_common.cpp
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

#include "omt_common.hpp"

#include <cassert>
#include <config.h> //for VERSION
#include <utility>

#include "debug.h"
#include "types.h"
#include "video_codec.h"
#define MOD_NAME "[omt] "

namespace{

/**
 * omt_setloggingcallback() is not yet available in release v1.0.0.14
 * This class detects if the function exists at compile time and either sets the callback
 * or prints a warning.
 *
 */
template<typename T, typename = void>
struct omt_set_callback_avail : std::false_type {  };

template<typename T>
struct omt_set_callback_avail<T, std::void_t<decltype(omt_setloggingcallback((std::declval<T>())))>>
        : std::true_type {  };

struct Omt_callback_setter{
        Omt_callback_setter(){ do_set_callback(omt_log_callback); }
        ~Omt_callback_setter() = default;

        template<typename T, std::enable_if_t<omt_set_callback_avail<T>::value, int> = 0>
        void do_set_callback(T callback){
                omt_setloggingcallback(callback);
        }

        template<typename T, std::enable_if_t<!omt_set_callback_avail<T>::value, int> = 0>
        void do_set_callback(T /*callback*/){
                log_msg(LOG_LEVEL_WARNING, MOD_NAME "libomt does not support setting logging callback\n");
        }
};

}

void ug_register_omt_log_callback(){
        [[maybe_unused]] static Omt_callback_setter omt_callback_setter;
}

void omt_log_callback(const char *msg){
        log_msg(LOG_LEVEL_INFO, MOD_NAME "[libomt] %s\n", msg);
}

void set_omt_sender_info(omt_send_t *send_handle){
        OMTSenderInfo info = {};
        std::string productName = "UltraGrid";
        std::string manufacturer = "CESNET";
        std::string version = VERSION;
        productName.copy(info.ProductName, OMT_MAX_STRING_LENGTH, 0);
        manufacturer.copy(info.Manufacturer, OMT_MAX_STRING_LENGTH, 0);
        version.copy(info.Version, OMT_MAX_STRING_LENGTH, 0);
        omt_send_setsenderinformation(send_handle, &info);
}

bool omt_frame_init_from_desc(OMTMediaFrame& f, const video_desc& frame_desc){
        f.Width = frame_desc.width;
        f.Height = frame_desc.height;
        if(frame_desc.color_spec != UYVY){
                log_msg(LOG_LEVEL_FATAL, MOD_NAME "Codec %s not supported\n", get_codec_name(frame_desc.color_spec));
                return false;
        }
        f.Codec = OMTCodec_UYVY;
        f.ColorSpace = OMTColorSpace_BT709;
        f.FrameRateN = frame_desc.fps * 1000.0;
        f.FrameRateD = 1000;
        return true;
}

void omt_frame_set_data(OMTMediaFrame& f, const video_frame& ug_frame){
        assert(ug_frame.tile_count == 1);
        f.Stride = vc_get_linesize(ug_frame.tiles[0].width, ug_frame.color_spec);
        f.Data = ug_frame.tiles[0].data;
        f.DataLength = ug_frame.tiles[0].data_len;
}

video_desc video_desc_from_omt_frame(const OMTMediaFrame *omt_frame){
        video_desc incoming_desc{};
        incoming_desc.fps = static_cast<double>(omt_frame->FrameRateN) / omt_frame->FrameRateD;
        incoming_desc.width = omt_frame->Width;
        incoming_desc.height = omt_frame->Height;
        incoming_desc.color_spec = UYVY; assert(omt_frame->Codec == OMTCodec_UYVY); //TODO
        incoming_desc.tile_count = 1;
        return incoming_desc;
}

