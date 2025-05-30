/**
 * @file   compat/qsort_s.h
 * @author Martin Pulec     <pulec@cesnet.cz>
 */
/*
 * Copyright (c) 2022-2025 CESNET
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

#ifndef COMPAT_QSORT_S_H_81DF21DE_370E_4C70_8559_600C1A9B6059
#define COMPAT_QSORT_S_H_81DF21DE_370E_4C70_8559_600C1A9B6059

#if !defined EXIT_SUCCESS // stdlib.h not yet included
#if defined  __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1 // we want qsort_s
#endif
#ifdef __cplusplus
#include <cstdlib>
#else
#include <stdlib.h>
#endif // !defined __cplusplus
#endif // !defined EXIT_SUCCESS

#if !defined __STDC_LIB_EXT1__
# if defined _WIN32
   // MS version of qsort_s with context as first arg of comparator
#  define QSORT_S_COMP_CTX_FIRST 1
# else
#  if defined __APPLE__ || defined __DragonFly__
#    define QSORT_S_COMP_CTX_FIRST 1 // Mac version of qsort_r() as well
#    define qsort_s(ptr, count, size, comp, context) qsort_r(ptr, count, size, context, comp)
#  else // POSIX/GNU version qsort_r(ptr, count, size, (*comp)(const void*,
        // const void*, void*), context) (identical to C11 qsort_s)
#    define qsort_s qsort_r
#  endif
# endif
#endif

#ifdef QSORT_S_COMP_CTX_FIRST
#define QSORT_S_COMP_DEFINE(name, a, b, context) int name(void *context, const void *a, const void *b)
#else
#define QSORT_S_COMP_DEFINE(name, a, b, context) int name(const void *a, const void *b, void *context)
#endif

#if defined __OpenBSD__ || defined __NetBSD__
typedef int (*compar_r)(const void *, const void *, void *);
static int compar_impl(const void *a, const void *b) __attribute__((unused));
static int
compar_impl(const void *a, const void *b)
{
        static _Thread_local compar_r comp    = NULL;
        static _Thread_local void    *context = NULL;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
        if (b == NULL) {
                comp = (void *) a;
        }
        if (a == NULL) {
                context = (void *) b;
        }
#pragma GCC diagnostic pop
        return comp(a, b, context);
}
static void qsort_r(void *base, size_t nmemb, size_t size,
                    int (*compar)(const void *, const void *, void *),
                    void *thunk) __attribute__((unused));
static void
qsort_r(void *base, size_t nmemb, size_t size,
        int (*compar)(const void *, const void *, void *), void *thunk)
{
        compar_impl(compar, NULL);
        compar_impl(NULL, thunk);
        return qsort(base, nmemb, size, compar_impl);
}
#endif

#endif // ! defined COMPAT_QSORT_S_H_81DF21DE_370E_4C70_8559_600C1A9B6059

