/**
 * @file   from_planar.h
 * @author Martin Pulec     <pulec@cesnet.cz>
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


#include "from_planar.h"

#include <assert.h>          // for assert
#include <stdint.h>          // for uint32_t, uintptr_t, uint16_t, uint64_t
#include <string.h>          // for memcpy

#include "compat/c23.h"    // for size_t, NULL, countof, nullptr, ptrdiff_t
#include "compat/endian.h" // BYTE_ORDER, BIG_ENDIAN
#include "types.h"         // for depth
#include "utils/misc.h"    // for get_cpu_core_count
#include "utils/worker.h"  // for task_run_parallel

#if BYTE_ORDER == BIG_ENDIAN
#define BYTE_SWAP(x) (3 - x)
#else
#define BYTE_SWAP(x) x
#endif

static void
gbrpXXle_to_r12l(struct from_planar_data d, const int in_depth, int rind, int gind, int bind)
{
        assert((uintptr_t) d.in_linesize[0] % 2 == 0);
        assert((uintptr_t) d.in_linesize[1] % 2 == 0);
        assert((uintptr_t) d.in_linesize[2] % 2 == 0);

#define S(x) ((x) >> (in_depth - 12))
        // clang-format off
        for (size_t y = 0; y < d.height; ++y) {
                const uint16_t *src_r = (const void *) (d.in_data[rind] + (d.in_linesize[rind] * y));
                const uint16_t *src_g = (const void *) (d.in_data[gind] + (d.in_linesize[gind] * y));
                const uint16_t *src_b = (const void *) (d.in_data[bind] + (d.in_linesize[bind] * y));
                unsigned char *dst =
                    (unsigned char *) d.out_data + (y * d.out_pitch);

                for (unsigned x = 0; x < d.width; x += 8) {
                        uint16_t tmpbuf[3][8];
                        if (x + 8 >= d.width) {
                                size_t remains = sizeof(uint16_t) * (d.width - x);
                                memcpy(tmpbuf[0], src_r, remains);
                                memcpy(tmpbuf[1], src_g, remains);
                                memcpy(tmpbuf[2], src_b, remains);
                                src_r = tmpbuf[0];
                                src_g = tmpbuf[1];
                                src_b = tmpbuf[2];
                        }
                        dst[BYTE_SWAP(0)] = S(*src_r) & 0xff;
                        dst[BYTE_SWAP(1)] = (S(*src_g) & 0xf) << 4 | S(*src_r++) >> 8;
                        dst[BYTE_SWAP(2)] = S(*src_g++) >> 4;
                        dst[BYTE_SWAP(3)] = S(*src_b) & 0xff;
                        dst[4 + BYTE_SWAP(0)] = (S(*src_r) & 0xf) << 4 | S(*src_b++) >> 8;
                        dst[4 + BYTE_SWAP(1)] = S(*src_r++) >> 4;
                        dst[4 + BYTE_SWAP(2)] = S(*src_g) & 0xff;
                        dst[4 + BYTE_SWAP(3)] = (S(*src_b) & 0xf) << 4 | S(*src_g++) >> 8;
                        dst[8 + BYTE_SWAP(0)] = S(*src_b++) >> 4;
                        dst[8 + BYTE_SWAP(1)] = S(*src_r) & 0xff;
                        dst[8 + BYTE_SWAP(2)] = (S(*src_g) & 0xf) << 4 | S(*src_r++) >> 8;
                        dst[8 + BYTE_SWAP(3)] = S(*src_g++) >> 4;
                        dst[12 + BYTE_SWAP(0)] = S(*src_b) & 0xff;
                        dst[12 + BYTE_SWAP(1)] = (S(*src_r) & 0xf) << 4 | S(*src_b++) >> 8;
                        dst[12 + BYTE_SWAP(2)] = S(*src_r++) >> 4;
                        dst[12 + BYTE_SWAP(3)] = S(*src_g) & 0xff;
                        dst[16 + BYTE_SWAP(0)] = (S(*src_b) & 0xf) << 4 | S(*src_g++) >> 8;
                        dst[16 + BYTE_SWAP(1)] = S(*src_b++) >> 4;
                        dst[16 + BYTE_SWAP(2)] = S(*src_r) & 0xff;
                        dst[16 + BYTE_SWAP(3)] = (S(*src_g) & 0xf) << 4 | S(*src_r++) >> 8;
                        dst[20 + BYTE_SWAP(0)] = S(*src_g++) >> 4;
                        dst[20 + BYTE_SWAP(1)] = S(*src_b) & 0xff;
                        dst[20 + BYTE_SWAP(2)] = (S(*src_r) & 0xf) << 4 | S(*src_b++) >> 8;
                        dst[20 + BYTE_SWAP(3)] = S(*src_r++) >> 4;;
                        dst[24 + BYTE_SWAP(0)] = S(*src_g) & 0xff;
                        dst[24 + BYTE_SWAP(1)] = (S(*src_b) & 0xf) << 4 | S(*src_g++) >> 8;
                        dst[24 + BYTE_SWAP(2)] = S(*src_b++) >> 4;
                        dst[24 + BYTE_SWAP(3)] = S(*src_r) & 0xff;
                        dst[28 + BYTE_SWAP(0)] = (S(*src_g) & 0xf) << 4 | S(*src_r++) >> 8;
                        dst[28 + BYTE_SWAP(1)] = S(*src_g++) >> 4;
                        dst[28 + BYTE_SWAP(2)] = S(*src_b) & 0xff;
                        dst[28 + BYTE_SWAP(3)] = (S(*src_r) & 0xf) << 4 | S(*src_b++) >> 8;
                        dst[32 + BYTE_SWAP(0)] = S(*src_r++) >> 4;
                        dst[32 + BYTE_SWAP(1)] = S(*src_g) & 0xff;
                        dst[32 + BYTE_SWAP(2)] = (S(*src_b) & 0xf) << 4 | S(*src_g++) >> 8;
                        dst[32 + BYTE_SWAP(3)] = S(*src_b++) >> 4;
                        dst += 36;
                }
        }
        // clang-format on
#undef S
}

/**
 * test with:
 * @code{.sh}
 * uv -t testcard:c=R12L -c lavc:e=libx265 -p change_pixfmt:RGBA -d gl
 * uv -t testcard:s=511x512c=R12L -c lavc:e=libx265 -p change_pixfmt:RGBA -d gl # irregular sz
 * # optionally also `--param decoder-use-codec=R12L` to ensure decoded codec
 * @endcode
 */
void
gbrp12le_to_r12l(struct from_planar_data d)
{
        gbrpXXle_to_r12l(d, DEPTH12, 2, 0, 1);
}

void
gbrp16le_to_r12l(struct from_planar_data d)
{
        gbrpXXle_to_r12l(d, DEPTH16, 2, 0, 1);
}

void
rgbp12le_to_r12l(struct from_planar_data d)
{
        gbrpXXle_to_r12l(d, DEPTH12, 0, 1, 2);
}

static void
rgbpXXle_to_rg48(struct from_planar_data d, const int in_depth, int rind, int gind, int bind)
{
        assert((uintptr_t) d.out_data % 2 == 0);
        assert((uintptr_t) d.in_data[0] % 2 == 0);
        assert((uintptr_t) d.in_data[1] % 2 == 0);
        assert((uintptr_t) d.in_data[2] % 2 == 0);

        for (ptrdiff_t y = 0; y < d.height; ++y) {
                const uint16_t *src_r = (const void *) (d.in_data[rind] + (d.in_linesize[rind] * y));
                const uint16_t *src_g = (const void *) (d.in_data[gind] + (d.in_linesize[gind] * y));
                const uint16_t *src_b = (const void *) (d.in_data[bind] + (d.in_linesize[bind] * y));
                uint16_t *dst = (void *) (d.out_data + (y * d.out_pitch));

                for (unsigned x = 0; x < d.width; ++x) {
                        *dst++ = *src_r++ << (16U - in_depth);
                        *dst++ = *src_g++ << (16U - in_depth);
                        *dst++ = *src_b++ << (16U - in_depth);
                }
        }
}

void
gbrp10le_to_rg48(struct from_planar_data d)
{
        rgbpXXle_to_rg48(d, DEPTH10, 2, 0, 1);
}

void
gbrp12le_to_rg48(struct from_planar_data d)
{
        rgbpXXle_to_rg48(d, DEPTH12, 2, 0, 1);
}

void
gbrp16le_to_rg48(struct from_planar_data d)
{
        rgbpXXle_to_rg48(d, DEPTH16, 2, 0, 1);
}

void
rgbp12le_to_rg48(struct from_planar_data d)
{
        rgbpXXle_to_rg48(d, DEPTH12, 0, 1, 2);
}

static void
gbrpXXle_to_r10k(struct from_planar_data d, const unsigned int in_depth,
                 int rind, int gind, int bind)
{
        // __builtin_trap();
        assert((uintptr_t) d.in_linesize[0] % 2 == 0);
        assert((uintptr_t) d.in_linesize[1] % 2 == 0);
        assert((uintptr_t) d.in_linesize[2] % 2 == 0);

        for (size_t y = 0; y < d.height; ++y) {
                const uint16_t *src_r = (const void *) (d.in_data[rind] + (d.in_linesize[rind] * y));
                const uint16_t *src_g = (const void *) (d.in_data[gind] + (d.in_linesize[gind] * y));
                const uint16_t *src_b = (const void *) (d.in_data[bind] + (d.in_linesize[bind] * y));
                unsigned char *dst = d.out_data + (y * d.out_pitch);

                for (unsigned x = 0; x < d.width; ++x) {
                        *dst++ = *src_r >> (in_depth - 8U);
                        *dst++ = ((*src_r++ >> (in_depth - 10U)) & 0x3U) << 6U | *src_g >> (in_depth - 6U);
                        *dst++ = ((*src_g++ >> (in_depth - 10U)) & 0xFU) << 4U | *src_b >> (in_depth - 4U);
                        *dst++ = ((*src_b++ >> (in_depth - 10U)) & 0x3FU) << 2U | 0x3U;
                }
        }
}

void
gbrp10le_to_r10k(struct from_planar_data d)
{
        gbrpXXle_to_r10k(d, DEPTH10, 2, 0, 1);
}

void
gbrp12le_to_r10k(struct from_planar_data d)
{
        gbrpXXle_to_r10k(d, DEPTH12, 2, 0, 1);
}

void
gbrp16le_to_r10k(struct from_planar_data d)
{
        gbrpXXle_to_r10k(d, DEPTH16, 2, 0, 1);
}

void
rgbp10le_to_r10k(struct from_planar_data d)
{
        gbrpXXle_to_r10k(d, DEPTH10, 0, 1, 2);
}

struct convert_task_data {
        decode_planar_func_t *convert;
        struct from_planar_data d;
};

static void *
convert_task(void *arg)
{
        struct convert_task_data *d = arg;
        d->convert(d->d);
        return nullptr;
}

// destiled from av_to_uv_convert
void decode_planar_parallel(decode_planar_func_t *dec, const struct from_planar_data d)
{
        const unsigned cpu_count = get_cpu_core_count();

        struct convert_task_data data[cpu_count];
        for (size_t i = 0; i < cpu_count; ++i) {
                unsigned row_height = (d.height / cpu_count) & ~1; // needs to be even
                data[i].convert     = dec;
                data[i].d           = d;
                data[i].d.out_data  = d.out_data + (i * row_height * d.out_pitch);

                for (unsigned plane = 0; plane < countof(d.in_data); ++plane) {
                        const int chroma_subs_log2 = 0;
                        data[i].d.in_data[plane] =
                            d.in_data[plane] +
                            ((i * row_height * d.in_linesize[plane]) >>
                             (plane == 0 ? 0 : chroma_subs_log2));
                }
                if (i == cpu_count - 1) {
                        row_height = d.height - (row_height * (cpu_count - 1));
                }
                data[i].d.height = row_height;
        }
        task_run_parallel(convert_task, (int) cpu_count, data, sizeof data[0], NULL);
}

