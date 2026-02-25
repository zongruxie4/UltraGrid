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

#ifndef FROM_PLANAR_H_E96E5C5D_6F9C_480D_AD6E_D0AD5149378F
#define FROM_PLANAR_H_E96E5C5D_6F9C_480D_AD6E_D0AD5149378F

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


/// functions to decode whole buffer of packed data to planar or packed
typedef void decode_planar_func_t(unsigned char *out_data, int out_pitch,
#ifdef __cplusplus
                                  const unsigned char *const *in_data, const int *in_linesize,
#else
                                  const unsigned char *const in_data[static 3], const int in_linesize[static 3],
#endif
                                  int width, int height);
decode_planar_func_t gbrp12le_to_r12l;
decode_planar_func_t gbrp16le_to_r12l;
decode_planar_func_t rgbp12le_to_r12l;
decode_planar_func_t gbrp10le_to_rg48;
decode_planar_func_t gbrp12le_to_rg48;
decode_planar_func_t gbrp16le_to_rg48;
decode_planar_func_t rgbp12le_to_rg48;
decode_planar_func_t gbrp10le_to_r10k;
decode_planar_func_t gbrp12le_to_r10k;
decode_planar_func_t gbrp16le_to_r10k;
decode_planar_func_t rgbp10le_to_r10k;
void decode_planar_parallel(decode_planar_func_t *dec, unsigned char *out_data,
                            int out_pitch, const unsigned char *const *in_data,
                            const int *in_linesize, int width, int height);
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // definedFROM_PLANAR_H_E96E5C5D_6F9C_480D_AD6E_D0AD5149378F
