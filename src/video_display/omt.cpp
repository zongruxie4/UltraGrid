/**
 * @file   video_display/omt.cpp
 * @author Martin Piatka     <piatka@cesnet.cz>
 */
/*
 * Copyright (c) 2026 CESNET, zájmové sdružení právnických osob
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

#define MOD_NAME "[omt] "
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "debug.h"
#include "lib_common.h"
#include "omt_common.hpp"
#include "video_display.h"
#include "video_frame.h"
#include "utils/misc.h"
#include "utils/thread.h"

namespace{
using frame_uniq = std::unique_ptr<video_frame, deleter_from_fcn<vf_free>>;

struct state_vdisp_omt{
        omt_send_uniq omt_send;

        OMTMediaFrame omt_video_frame{};

        video_desc desc{};

        std::mutex mutex;
        std::condition_variable free_frame_cv;
        std::vector<frame_uniq> free_frames;

        std::condition_variable video_frame_cv;
        std::queue<frame_uniq> frame_queue;

        std::thread worker;
        std::string sender_name = "UltraGrid";
        OMTQuality quality = OMTQuality_Medium;
};

void send_video_frame(state_vdisp_omt *s, frame_uniq frame){
        omt_frame_init_from_desc(s->omt_video_frame, video_desc_from_frame(frame.get()));
        omt_frame_set_data(s->omt_video_frame, *frame);

        omt_send(s->omt_send.get(), &s->omt_video_frame);

        std::unique_lock<std::mutex> lock(s->mutex);
        s->free_frames.push_back(std::move(frame));
        lock.unlock();
        s->free_frame_cv.notify_one();
}

void omt_disp_worker(state_vdisp_omt *s){
        set_thread_name("OMT display worker");

        while(true){
                std::unique_lock lock(s->mutex);
                s->video_frame_cv.wait(lock, [s]{ return !s->frame_queue.empty(); });
                auto frame = std::move(s->frame_queue.front());
                s->frame_queue.pop();
                lock.unlock();

                if(!frame){
                        break;
                }

                send_video_frame(s, std::move(frame));
        }
}

void init_send(state_vdisp_omt *s){
        s->omt_send.reset(omt_send_create(s->sender_name.c_str(), s->quality));
        set_omt_sender_info(s->omt_send.get());
        s->omt_video_frame.Type = OMTFrameType_Video;
        s->omt_video_frame.Timestamp = -1;
}

void *display_omt_init(module *parent, const char *fmt, unsigned int flags){
        auto s = std::make_unique<state_vdisp_omt>();

        ug_register_omt_log_callback();

        constexpr size_t max_frames = 3;
        s->free_frames.resize(max_frames);

        init_send(s.get());

        s->worker = std::thread(omt_disp_worker, s.get());

        return s.release();
}

void display_omt_done(void *state){
        auto s = static_cast<state_vdisp_omt *>(state);

        if(s->worker.joinable()){
                s->worker.join();
        }

        delete s;
}

video_frame *display_omt_getf(void *state){
        auto s = static_cast<state_vdisp_omt *>(state);

        std::unique_lock lock(s->mutex);
        s->free_frame_cv.wait(lock, [s]{ return !s->free_frames.empty(); });

        auto frame = std::move(s->free_frames.back());
        s->free_frames.pop_back();

        if(!frame || !video_desc_eq(s->desc, video_desc_from_frame(frame.get()))){
                frame.reset(vf_alloc_desc_data(s->desc));
        }

        return frame.release();
}

bool display_omt_putf(void *state, video_frame *frame, long long timeout_ns){
        auto s = static_cast<state_vdisp_omt *>(state);
        auto f = frame_uniq(frame);

        std::unique_lock lock(s->mutex);
        s->frame_queue.push(std::move(f));
        lock.unlock();
        s->video_frame_cv.notify_one();

        return true;
}

bool display_omt_reconfigure(void *state, video_desc desc){
        auto s = static_cast<state_vdisp_omt *>(state);
        if(desc.color_spec != UYVY)
                return false;

        s->desc = desc;
        return true;
}

bool display_omt_get_property(void */*state*/, int property, void *val, size_t *len){
        codec_t codecs[] = {UYVY};
        interlacing_t supported_il_modes[] = {PROGRESSIVE};
        int rgb_shift[] = {0, 8, 16};

        switch (property) {
        case DISPLAY_PROPERTY_CODECS:
                if(sizeof(codecs) <= *len) {
                        memcpy(val, codecs, sizeof(codecs));
                } else {
                        return false;
                }

                *len = sizeof(codecs);
                break;
        case DISPLAY_PROPERTY_RGB_SHIFT:
                if(sizeof(rgb_shift) > *len) {
                        return false;
                }
                memcpy(val, rgb_shift, sizeof(rgb_shift));
                *len = sizeof(rgb_shift);
                break;
        case DISPLAY_PROPERTY_BUF_PITCH:
                *static_cast<int *>(val) = PITCH_DEFAULT;
                *len = sizeof(int);
                break;
        case DISPLAY_PROPERTY_SUPPORTED_IL_MODES:
                if(sizeof(supported_il_modes) <= *len) {
                        memcpy(val, supported_il_modes, sizeof(supported_il_modes));
                } else {
                        return false;
                }
                *len = sizeof(supported_il_modes);
                break;
        default:
                return false;
        }
        return true;
}

void display_omt_probe(device_info **available_cards, int *count, void(**/*deleter*/)(void *)){
        //TODO
        *available_cards = nullptr;
        *count = 0;
}
}

static constexpr video_display_info display_omt_info = {
        display_omt_probe,
        display_omt_init,
        nullptr, // _run
        display_omt_done,
        display_omt_getf,
        display_omt_putf,
        display_omt_reconfigure,
        display_omt_get_property,
        nullptr, // _put_audio_frame
        nullptr, // _reconfigure_audio
        MOD_NAME,
};

REGISTER_MODULE(omt, &display_omt_info, LIBRARY_CLASS_VIDEO_DISPLAY, VIDEO_DISPLAY_ABI_VERSION);
