/**
 * @file   jpegxs/from_jpegxs_conv.c
 * @author Jan Frejlach     <536577@mail.muni.cz>
 */
/*
 * Copyright (c) 2026 CESNET
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

#include "jpegxs_conv.h"

#include <assert.h>                // for assert
#include <stdint.h>                // for uint16_t, uint8_t, uint32_t
#include <svt-jpegxs/SvtJpegxs.h>
#include <string.h>

#include "from_planar.h"           // for rgbp12le_to_r12l
#include "types.h"
#include "video_codec.h"

struct jpegxs_to_uv_conversion {
        ColourFormat_t src;
        codec_t dst;
        int in_bpp;
        decode_planar_func_t *convert_external;
};

static const struct jpegxs_to_uv_conversion jpegxs_to_uv_conversions[] = {
        { COLOUR_FORMAT_PLANAR_YUV422,        UYVY,             1, yuv422p_to_uyvy  },
        { COLOUR_FORMAT_PLANAR_YUV422,        YUYV,             1, yuv422p_to_yuyv  },
        { COLOUR_FORMAT_PLANAR_YUV420,        I420,             1, yuv420_to_i420   },
        { COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, RGB,              1, rgbp_to_rgb      },
        { COLOUR_FORMAT_PLANAR_YUV422,        v210,             2, yuv422p10le_to_v210},
        { COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, R10k,             2, rgbp10le_to_r10k },
        { COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, R12L,             2, rgbp12le_to_r12l },
        { COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, RG48,             2, rgbp12le_to_rg48 },
        { COLOUR_FORMAT_INVALID,              VIDEO_CODEC_NONE, 0, NULL             }
};

const struct jpegxs_to_uv_conversion *get_jpegxs_to_uv_conversion(codec_t codec) {

        const struct jpegxs_to_uv_conversion *conv = jpegxs_to_uv_conversions;
        while (conv->dst != VIDEO_CODEC_NONE) {
                if (conv->dst == codec)
                        return conv;
                conv++;
        }

        return NULL;
}

void jpegxs_to_uv_convert(const struct jpegxs_to_uv_conversion   *conv,
                          const svt_jpeg_xs_image_buffer_t *src, int width,
                          int height, uint8_t *dst)
{
        assert (conv->convert_external);
        struct from_planar_data d = {
                .width          = width,
                .height         = height,
                .out_data       = dst,
                .out_pitch      = vc_get_linesize(width, conv->dst),
                .in_data[0]     = src->data_yuv[0],
                .in_data[1]     = src->data_yuv[1],
                .in_data[2]     = src->data_yuv[2],
                .in_linesize[0] = src->stride[0] * conv->in_bpp,
                .in_linesize[1] = src->stride[1] * conv->in_bpp,
                .in_linesize[2] = src->stride[2] * conv->in_bpp,
        };
        int num_threads = 0;
        if (conv->convert_external == yuv420_to_i420) {
                num_threads = 1; // no proper support for parallel decode
        }
        decode_planar_parallel(conv->convert_external, d, num_threads);
}
