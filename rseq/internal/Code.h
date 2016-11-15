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
#include <cstdint>

namespace rseq {
namespace internal {

class Code {
 public:
  // The rseq load and store functions return 1 if there was an interruption,
  // and 0 otherwise.
  typedef int (*RseqLoadFunc)(unsigned long* dst, unsigned long* src);
  typedef int (*RseqStoreFunc)(unsigned long* dst, unsigned long val);

  static Code* initForId(std::uint32_t id, std::atomic<int>* threadCachedCpu);

  RseqLoadFunc rseqLoadFunc();
  RseqStoreFunc rseqStoreFunc();
  RseqStoreFunc rseqStoreFenceFunc();

  void blockRseqOps();
  void unblockRseqOps();

 private:
  unsigned char code_[54]; // See Code.cpp to see where 54 comes from.
};

} // namespace internal
} // namespace rseq
