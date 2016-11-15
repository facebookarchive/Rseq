/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <unistd.h>

#include "rseq/internal/Mutex.h"

namespace rseq {
namespace internal {

namespace detail {
extern mutex::OnceFlag numCpusOnceFlag;
} // namespace detail

// std::thread::hardware_concurrency() is surprisingly slow. This just caches
// the result.
inline int numCpus() {
  static int result;
  mutex::callOnce(detail::numCpusOnceFlag, []() {
    result = sysconf(_SC_NPROCESSORS_ONLN);
  });
  return result;
}

} // namespace internal
} // namespace rseq
