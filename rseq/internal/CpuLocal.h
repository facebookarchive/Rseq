/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <new>

#include "rseq/internal/CachelinePadded.h"
#include "rseq/internal/NumCpus.h"
#include "rseq/internal/OsMem.h"


namespace rseq {
namespace internal {

template <typename T>
class CpuLocal {
 public:
  CpuLocal() {
    void* mem = os_mem::allocate(sizeof(ElemType) * numCpus());
    elements_ = static_cast<ElemType*>(mem);
    for (int i = 0; i < numCpus(); ++i) {
      new (&elements_[i]) ElemType;
    }
  }

  ~CpuLocal() {
    for (int i = 0; i < numCpus(); ++i) {
      elements_[i].~ElemType();
    }
    os_mem::free(elements_, sizeof(ElemType) * numCpus());
  }

  T* forCpu(int i) {
    return elements_[i].get();
  }

 private:
  // This saves us some typing, and is needed for explicit destructor invocation
  // (which doesn't parse with template types).
  typedef CachelinePadded<T> ElemType;
  ElemType* elements_;
};

} // namespace internal
} // namespace rseq
