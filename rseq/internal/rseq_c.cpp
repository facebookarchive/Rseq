/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <unistd.h>

#include <stdio.h>

#include <cstdlib>
#include <exception>

#include "rseq/internal/Errors.h"
#include "rseq/internal/Rseq.h"

extern "C" {

__thread int (*rseq_load_trampoline)(unsigned long* dst, unsigned long* src);
__thread int (*rseq_store_trampoline)(unsigned long* dst, unsigned long val);
__thread int (*rseq_store_fence_trampoline)(
    unsigned long* dst, unsigned long val);

int rseq_begin_slow_path() {
  rseq::internal::errors::AbortOnError aoe;
  return rseq::internal::beginSlowPath();
}

void rseq_end() {
  rseq::internal::errors::AbortOnError aoe;
  rseq::internal::end();
}

void rseq_fence_with(int shard) {
  rseq::internal::errors::AbortOnError aoe;
  rseq::internal::fenceWith(shard);
}

void rseq_fence() {
  rseq::internal::errors::AbortOnError aoe;
  rseq::internal::fence();
}

} /* extern "C" */
