/**
 * @file   video_rxtx/omt.cpp
 * @author Martin Piatka     <piatka@cesnet.cz>
 */
/*
 * Copyright (c) 2021-2026 CESNET, zájmové sdružení právických osob
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

#ifndef OMT_HPP_D74C07C80E5A4107B06725B233491BAC
#define OMT_HPP_D74C07C80E5A4107B06725B233491BAC

#include <queue>
#include <condition_variable>
#include <libomt.h>
#include "video_rxtx.hpp"

class omt_video_rxtx final : public video_rxtx{
public:
	omt_video_rxtx(std::map<std::string, param_u> const&);
	~omt_video_rxtx() override;

private:
	static void *recv_worker(void*);
	void send_frame(std::shared_ptr<video_frame>) noexcept override;
	void *receiver_loop();
	auto get_receiver_thread() noexcept -> void*(*)(void *arg) override;

	omt_receive_t *omt_recv_handle = nullptr;
	omt_send_t *omt_send_handle = nullptr;

	OMTMediaFrame send_video_frame{};
	std::shared_ptr<video_frame> current_video_frame;

	display *disp = nullptr;
	video_desc desc{};
	display *display_device;
};

#endif //OMT_HPP_D74C07C80E5A4107B06725B233491BAC
