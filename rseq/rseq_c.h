/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "rseq/internal/Likely.h"
#include "rseq/internal/rseq_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A 64-bit type; this is what "inhabits" rseq slots. */
typedef unsigned long rseq_value_t;

/* Rseq slots to which you can do rseq-protected loads and stores. */
typedef struct {
  /* This union tricks gcc into not complaining about the strict-aliasing
   * violation here. I don't think it actually fixes the undefined behavior, but
   * as a practical matter the utility of being able to cast to atomic types is
   * more important. */
  union {
    volatile rseq_value_t item;
    volatile char aliasing_goo[sizeof(rseq_value_t)];
  };
} rseq_repr_t;


inline int rseq_begin() {
  int ret = rseq_thread_cached_cpu;
  if (RSEQ_UNLIKELY(ret < 0)) {
    ret = rseq_begin_slow_path();
  }
  /* Good enough for an acquire barrier on x86. */
  __asm__ volatile("" : : : "memory");
  return ret;
}

inline int rseq_load(rseq_value_t *dst, rseq_repr_t *src) {
  /* Note: this goes through dynamically generated code, which will prevent
     compiler reordering. */
  return RSEQ_LIKELY(!rseq_load_trampoline(dst, (unsigned long*)src));
}

inline int rseq_store(rseq_repr_t *dst, rseq_value_t val) {
  /* Same here. */
  return RSEQ_LIKELY(!rseq_store_trampoline((unsigned long*)dst, val));
}

inline int rseq_store_fence(rseq_repr_t *dst, rseq_value_t val) {
  /* And here. */
  return RSEQ_LIKELY(!rseq_store_fence_trampoline((unsigned long*)dst, val));
}

inline int rseq_validate() {
  rseq_repr_t dummy;
  return rseq_store(&dummy, 0);
}

void rseq_end();
void rseq_fence_with(int shard);
void rseq_fence();


#ifdef __cplusplus
} /* extern "C" */
#endif
