/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>

#include "rseq/internal/Errors.h"
#include "rseq/internal/rseq_c.h"

namespace rseq {
namespace internal {

// Internal equivalents of public functions.
// We put the error wrappers in the header file so that no exception logic lives
// in librseq.


int beginSlowPath();
void end();
void fenceWith(int shard);
void fence();

inline int beginSlowPathWrapper() {
  errors::ThrowOnError thrower;
  return beginSlowPath();
};

inline void endWrapper() {
  errors::ThrowOnError thrower;
  end();
}

inline void fenceWithWrapper(int shard) {
  errors::ThrowOnError thrower;
  fenceWith(shard);
}

inline void fenceWrapper() {
  errors::ThrowOnError thrower;
  fence();
}

inline std::atomic<int>* threadCachedCpu() {
  return reinterpret_cast<std::atomic<int>*>(
      const_cast<int*>(&rseq_thread_cached_cpu));
}

} // namespace internal
} // namespace rseq
