/**
 * @file   omt_common.hpp
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

#ifndef OMT_COMMON_HPP_5963748ABF904BA79B6C22CAC8500636
#define OMT_COMMON_HPP_5963748ABF904BA79B6C22CAC8500636

#include <libomt.h>
#include <memory>

#include "types.h"
#include "utils/misc.h"

using omt_receive_uniq = std::unique_ptr<omt_receive_t, deleter_from_fcn<omt_receive_destroy>>;
using omt_send_uniq = std::unique_ptr<omt_send_t, deleter_from_fcn<omt_send_destroy>>;

void omt_log_callback(const char *msg);
void set_omt_sender_info(omt_send_t *send_handle);
bool omt_frame_init_from_desc(OMTMediaFrame& f, const video_desc& frame_desc);
void omt_frame_set_data(OMTMediaFrame& f, const video_frame& ug_frame);
video_desc video_desc_from_omt_frame(const OMTMediaFrame *omt_frame);

#endif //OMT_COMMON_HPP_5963748ABF904BA79B6C22CAC8500636
