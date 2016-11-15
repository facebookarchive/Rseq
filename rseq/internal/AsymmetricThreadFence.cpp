/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/AsymmetricThreadFence.h"

#include <sys/mman.h>

#include <cstdint>

#include "rseq/internal/Errors.h"
#include "rseq/internal/Mutex.h"

namespace rseq {
namespace internal {

// TODO: There's a lot we can do to speed this up if we like, with varying
// time/space tradeoffs:
// - Can make this lock-free, or allocate from a pool of threads.
// - Give each thread its own page for mprotect operations. This lets us cycle
//   it through: "R/W/X" -> "R/W" -> "R" -> "None", doing 4 mprotects for 3
//   heavy fences (instead of the 6 mprotects we need for the current
//   mechanism).
// - Give each thread a range of pages to operate on; allocate N pages instead
//   of 1. Permissions are lowered one at a time, and raised in batch after
//   we've exhausted all of the lowerings we can.
// - We can delegate the permission raising to a helper thread. Wouldn't save
//   much time, but could save pages.

static mutex::Mutex mu;
void asymmetricThreadFenceHeavy() {
  static char page[8192];

  std::uintptr_t pageInt = reinterpret_cast<std::uintptr_t>(page);
  std::uintptr_t alignedInt = (pageInt + 4096 - 1) & ~(4096 - 1);
  char* aligned = reinterpret_cast<char*>(alignedInt);

  mutex::LockGuard<mutex::Mutex> lg(mu);

  // Make this volatile so that we know the debugger can see it if we die (for
  // simplicity, we don't include it in the error message)
  volatile int err = mprotect(aligned, 4096, PROT_READ | PROT_WRITE);
  if (err) {
    errors::fatalError(
        "First mprotect in asymmetricThreadFenceHeavy failed.\n");
  }

  // Page must be dirty to trigger the IPI.
  *static_cast<volatile char*>(aligned) = 0;

  err = mprotect(aligned, 4096, PROT_READ);
  if (err) {
    errors::fatalError(
        "Second mprotect in asymmetricThreadFenceHeavy failed.\n");
  }
}

} // namespace internal
} // namespace rseq
