/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/OsMem.h"

#include <setjmp.h>
#include <signal.h>

#include <atomic>
#include <cstring>

#include <gtest/gtest.h>

#include "rseq/internal/Errors.h"

using namespace rseq::internal;
using namespace rseq::internal::os_mem;

TEST(OsMem, SanityCheck) {
  const int kAllocSize1 = 123456;
  const int kAllocSize2 = 12345;

  void* alloc1 = allocate(kAllocSize1);
  void* alloc2 = allocate(kAllocSize2);

  unsigned char* arr1 = reinterpret_cast<unsigned char*>(alloc1);
  unsigned char* arr2 = reinterpret_cast<unsigned char*>(alloc2);

  for (int i = 0; i < kAllocSize1; ++i) {
    arr1[i] = 111;
  }
  for (int i = 0; i < kAllocSize2; ++i) {
    arr2[i] = 222;
  }

  for (int i = 0; i < kAllocSize1; ++i) {
    EXPECT_EQ(111, arr1[i]);
  }
  free(alloc1, kAllocSize1);

  for (int i = 0; i < kAllocSize2; ++i) {
    EXPECT_EQ(222, arr2[i]);
  }
  free(alloc2, kAllocSize2);
}

#if __EXCEPTIONS
TEST(OsMem, ThrowsOnFailure) {
  errors::ThrowOnError thrower;

  bool failed = false;
  const std::size_t kTooBig = 1ULL << 48;
  try {
    allocate(kTooBig);
  } catch (...) {
    failed = true;
  }
  EXPECT_TRUE(failed);
  failed = false;
  try {
    allocateExecutable(kTooBig);
  } catch (...) {
    failed = true;
  }
  EXPECT_TRUE(failed);
}
#endif // __EXCEPTIONS

TEST(OsMem, AllocatesExecutable) {
  const unsigned char return12345Template[] = {
    // mov $12345, %eax ; (12345 = 0x3039)
    0xb8, 0x39, 0x30, 0x00, 0x00,
    // retq
    0xc3,
  };

  void* code = allocateExecutable(sizeof(return12345Template));
  std::memcpy(code, return12345Template, sizeof(return12345Template));
  int (*fn)() = reinterpret_cast<int (*)()>(code);
  EXPECT_EQ(12345, fn());
  free(code, sizeof(return12345Template));
}

TEST(OsMem, Frees) {
  // These can't be stack variables, since we need to know their address
  // (without being told) in the signal handler. We make them thread-local to
  // avoid any parallel testing trickiness.
  static __thread void* volatile alloc;
  static __thread volatile bool segfaulted;
  static __thread jmp_buf returnFromSegfault;

  alloc = nullptr;
  segfaulted = false;

  struct sigaction oldHandler;
  struct sigaction newHandler;

  void (*segfaultHandler)(int, siginfo_t*, void*)
      = +[](int signo, siginfo_t* info, void* ucontext) {
        EXPECT_EQ(SIGSEGV, signo);
        // EXPECT_EQ is a little screwy with regards to volatile pointer (note:
        // not pointer *to* volatile) arguments. We copy its argument into a
        // non-volatile pointer to help it out.
        void* allocCopy = alloc;
        EXPECT_EQ(allocCopy, info->si_addr);
        segfaulted = true;
        // We setjmp(returnFromSegfault) before triggering the segfault.
        longjmp(returnFromSegfault, 1);
      };

  std::memset(&newHandler, 0, sizeof(newHandler));
  newHandler.sa_sigaction = segfaultHandler;
  newHandler.sa_flags = SA_SIGINFO;
  int err = sigaction(SIGSEGV, &newHandler, &oldHandler);
  ASSERT_EQ(0, err);

  alloc = allocate(1);
  volatile char* c = static_cast<char*>(alloc);
  *c = 123;

  free(alloc, 1);
  EXPECT_FALSE(segfaulted);

  // BEGIN MAGIC
  if (!setjmp(returnFromSegfault)) {
    // Not returning from the segfault handler; cause a segfault.
    *c;
  } else {
    // Returning from the segfault handler.
    EXPECT_TRUE(segfaulted);
  }
  // END MAGIC

  // Go back to the previous signal handler (probably crashing).
  sigaction(SIGSEGV, &oldHandler, nullptr);
}
