/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* We want this in C-land so that C users can get the fast inlined versions too.
 * It turns out to be slightly faster to have these return false on success and
 * true on failure, so we invert the result in the wrapper functions, hoping
 * that the compiler can use its visibility into them to avoid having to do its
 * own inversion. */
extern __thread int (*rseq_load_trampoline)(
    unsigned long* dst, unsigned long* src);
extern __thread int (*rseq_store_trampoline)(
    unsigned long* dst, unsigned long val);
extern __thread int (*rseq_store_fence_trampoline)(
    unsigned long* dst, unsigned long val);

inline volatile int* rseq_thread_cached_cpu() {
  static __thread volatile int cpu = -1;
  return &cpu;
}

int rseq_begin_slow_path();

#ifdef __cplusplus
} /* extern "C" */
#endif
