// SPDX-License-Identifier: MIT
//
// Copyright (c) 2011 Google, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#ifndef	_SYS_CITYHASH_H
#define	_SYS_CITYHASH_H extern __attribute__((visibility("default")))

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Define 1/2/3-argument specialized versions of cityhash4, which can reduce
 * instruction count (especially multiplication) on some 32-bit arches.
 */
_SYS_CITYHASH_H uint64_t cityhash1(uint64_t);
_SYS_CITYHASH_H uint64_t cityhash2(uint64_t, uint64_t);
_SYS_CITYHASH_H uint64_t cityhash3(uint64_t, uint64_t, uint64_t);
_SYS_CITYHASH_H uint64_t cityhash4(uint64_t, uint64_t, uint64_t, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CITYHASH_H */
