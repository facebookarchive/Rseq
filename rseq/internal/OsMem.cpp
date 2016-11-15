/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <sys/mman.h>

#include <cstdint>
#include <stdexcept>

#include "rseq/internal/Errors.h"

namespace rseq {
namespace internal {
namespace os_mem {

static void* mmapWithPermissions(std::size_t bytes, int prot) {
  // If we die in this method, it'd be helpful to know the arguments; make sure
  // they're available in the debugger.
  volatile int bytesCopy = bytes;
  volatile int protCopy = prot;

  void* alloc = mmap(
    nullptr,
    bytes,
    prot,
    MAP_PRIVATE | MAP_ANONYMOUS,
    -1,
    0);
  if (alloc == MAP_FAILED) {
    errors::fatalError("mmap failed.");
  }
  return alloc;
}

void* allocate(std::size_t bytes) {
  return mmapWithPermissions(bytes, PROT_READ | PROT_WRITE);
}

void* allocateExecutable(std::size_t bytes) {
  return mmapWithPermissions(bytes, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void free(void* ptr, std::size_t bytes) {
  // Note that we may throw, even though this is on a deallocation path. So if
  // we get called with an invalid argument during exception unwinding, we'll
  // crash the process. This is an acceptable penalty for passing invalid
  // pointers to your memory allocator.
  const int kPageSize = 4096;
  std::uintptr_t ptrInt = reinterpret_cast<std::uintptr_t>(ptr);
  if (ptrInt & (kPageSize - 1)) {
    errors::fatalError("Improperly aligned pointer");
  }
  int err = munmap(ptr, bytes);
  if (err != 0) {
    errors::fatalError("munmap failed");
  }
}

} // namespace os_mem
} // namespace internal
} // namespace rseq
