/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Mutex.h"

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace rseq {
namespace internal {
namespace mutex {

void Mutex::futexWait(std::uint32_t val) {
  // We ignore errors here; it just means we'll spin a little extra.
  syscall(
      __NR_futex,
      &state_,
      FUTEX_WAIT | FUTEX_PRIVATE_FLAG,
      val,
      nullptr,
      nullptr,
      0);
}

void Mutex::futexWake(int num) {
  // Ignore errors here, too; it probably means a destructor race.
  syscall(
      __NR_futex,
      &state_,
      FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
      num,
      nullptr,
      nullptr,
      0);
}

} // namespace mutex
} // namespace internal
} // namespace rseq
