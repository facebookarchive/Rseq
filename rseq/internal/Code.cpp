/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Code.h"

#include <cstring>

#include "rseq/internal/CachelinePadded.h"
#include "rseq/internal/Mutex.h"
#include "rseq/internal/OsMem.h"

namespace rseq {
namespace internal {

static const unsigned char codeTemplate[] = {
  // 8-byte load code. Prototype is:
  // int (*)(unsigned long* dst, unsigned long* src);

  // Do the load
  //                       mov (%rsi), %rax
  /* offset   0: */        0x48, 0x8b, 0x06,

  // Store it into *dst
  //                       mov %rax, (%rdi)
  /* offset   3: */        0x48, 0x89, 0x07,

  // Return success! (i.e. 0)
  //                       xor %eax, %eax
  /* offset   6: */        0x31, 0xc0,
  //                       retq
  /* offset   8: */        0xc3,

  // Padding bytes
  /* offset   9: */        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  // 8-byte store code. Prototype is:
  // int (*)(unsigned long* dst, unsigned long val);

  // Do the store.
  //                       mov %rsi, (%rdi)
  /* offset  16: */        0x48, 0x89, 0x37,

  // Return success! (i.e. 0)
  //                       xor %eax, %eax
  /* offset  19: */        0x31, 0xc0,
  //                       retq
  /* offset  21: */        0xc3,


  // Padding bytes
  /* offset  22: */        0x00, 0x00,


  // 8-byte store-fence code. Prototype is:
  // int (*)(unsigned long* dst, unsigned long val);

  // Do the store (via xchg).
  //                       xchg %rsi, (%rdi)
  /* offset  24: */        0x48, 0x87, 0x37,

  // Return success! (i.e. 0)
  //                       xor %eax, %eax
  /* offset  27: */        0x31, 0xc0,
  //                       retq
  /* offset  29: */        0xc3,


  // Padding bytes
  /* offset  30: */        0x00, 0x00,


  // Failure path.
  // This code is shared by all the load and store paths above.
  // The initial instruction of each path is patched to be a jump to here.

  // Store -1 into the threadCachedCpu variable.
  // The 42s get replaced with a pointer to the owning thread's threadCachedCpu
  // variable.
  //                       movabs $0x4242424242424242, %rax
  /* offset  32: */        0x48, 0xb8,
  /* offset  34: */        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
  //                       movl   $-1, (%rax)
  /* offset  42: */        0xc7, 0x00, 0xff, 0xff, 0xff, 0xff,

  // Return failure :( (i.e. 1).
  //                       mov $1, %eax
  /* offset  48: */        0xb8, 0x01, 0x00, 0x00, 0x00,
  //                       retq
  /* offset  53: */        0xc3
};


const static int kLoadOffset = 0;
const static int kStoreOffset = 16;
const static int kStoreFenceOffset = 24;
const static int kReturnFailureOffset = 32;
const static int kThreadCachedCpuOffset = 34;


const static int kJmpInstructionSize = 2;

const int kLoadToFailureJmpSize
    = kReturnFailureOffset - kLoadOffset - kJmpInstructionSize;
const int kStoreToFailureJmpSize
    = kReturnFailureOffset - kStoreOffset - kJmpInstructionSize;
const int kStoreFenceToFailureJmpSize
    = kReturnFailureOffset - kStoreFenceOffset - kJmpInstructionSize;


const std::uint16_t kJmpBytecode = 0xeb;
const std::uint16_t kLoadReplacement
    = kJmpBytecode | (kLoadToFailureJmpSize << 8);
const std::uint16_t kStoreReplacement
    = kJmpBytecode | (kStoreToFailureJmpSize << 8);
const std::uint16_t kStoreFenceReplacement
    = kJmpBytecode | (kStoreFenceToFailureJmpSize << 8);


static mutex::OnceFlag codePagesOnceFlag;
static CachelinePadded<Code>* codePages;

// static
Code* Code::initForId(std::uint32_t id, std::atomic<int>* threadCachedCpu) {
  static_assert(
      sizeof(codeTemplate) == sizeof(Code::code_),
      "codeTemplate and code_ storage size must match.");

  mutex::callOnce(codePagesOnceFlag, []() {
    // We get kMaxGlobalThreads from the kernel limit. This reserves 256MB of
    // address space, but pages are lazily allocated, so the actual cost is much
    // smaller.
    const int kMaxGlobalThreads = 1 << 22;
    const int kMemToReserve = kMaxGlobalThreads * sizeof(CachelinePadded<Code>);

    void* alloc = os_mem::allocateExecutable(kMemToReserve);
    codePages = static_cast<CachelinePadded<Code>*>(alloc);
  });
  Code* code = codePages[id].get();
  std::memcpy(code->code_, codeTemplate, sizeof(codeTemplate));
  std::memcpy(
      &code->code_[kThreadCachedCpuOffset],
      &threadCachedCpu,
      sizeof(threadCachedCpu));
  return code;
}

Code::RseqLoadFunc Code::rseqLoadFunc() {
  return reinterpret_cast<RseqLoadFunc>(&code_[kLoadOffset]);
}

Code::RseqStoreFunc Code::rseqStoreFunc() {
  return reinterpret_cast<RseqStoreFunc>(&code_[kStoreOffset]);
}

Code::RseqStoreFunc Code::rseqStoreFenceFunc() {
  return reinterpret_cast<RseqStoreFunc>(&code_[kStoreFenceOffset]);
}

void Code::blockRseqOps() {
  std::atomic<std::uint16_t>* load =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kLoadOffset]);
  std::atomic<std::uint16_t>* store =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kStoreOffset]);
  std::atomic<std::uint16_t>* storeFence =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kStoreFenceOffset]);
  load->store(kLoadReplacement, std::memory_order_relaxed);
  store->store(kStoreReplacement, std::memory_order_relaxed);
  storeFence->store(kStoreFenceReplacement, std::memory_order_relaxed);
}

void Code::unblockRseqOps() {
  const std::uint16_t kLoadBytes = 0x8b48;
  const std::uint16_t kStoreBytes = 0x8948;
  const std::uint16_t kStoreFenceBytes = 0x8748;

  std::atomic<std::uint16_t>* load =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kLoadOffset]);
  std::atomic<std::uint16_t>* store =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kStoreOffset]);
  std::atomic<std::uint16_t>* storeFence =
      reinterpret_cast<std::atomic<std::uint16_t>*>(&code_[kStoreFenceOffset]);

  load->store(kLoadBytes, std::memory_order_relaxed);
  store->store(kStoreBytes, std::memory_order_relaxed);
  storeFence->store(kStoreFenceBytes, std::memory_order_relaxed);
}
} // namespace internal
} // namespace rseq
