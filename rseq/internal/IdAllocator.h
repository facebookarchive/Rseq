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

#include "rseq/internal/Mutex.h"
#include "rseq/internal/OsMem.h"

namespace rseq {
namespace internal {

// This can block when acquiring or releasing an Id, but doing an Id->owner
// lookup is lock-free and fast.
// This never returns an Id of 0; you can use that as "null".
// This is guaranteed to return either an Id that has been acquired and then
// released, or, if no such Id exists, the smallest positive uint32_t that has
// not already been allocated.
// TODO: if we ever start using this more than in a handful of places, we should
// type-erase everything; the identity of T doesn't matter.
template <typename T>
class IdAllocator {
 public:
   // maxElements should include the null element. If you need 10 items + null,
   // maxElement should be 11.
   explicit IdAllocator(std::uint32_t maxElements) : maxElements_(maxElements) {
     mu_.init();

     freeListHead_ = 0;
     // We never allocate id 0, so we can use it as null in the linked list of
     // free ids.
     firstUntouchedId_ = 1;

     void* mem = os_mem::allocate(maxElements_ * sizeof(FreeNodeOrItem));
     items_ = static_cast<FreeNodeOrItem*>(mem);
   }

   ~IdAllocator() {
     os_mem::free(items_, maxElements_ * sizeof(FreeNodeOrItem));
   }

   std::uint32_t allocate(T* owner) {
     mutex::LockGuard<mutex::Mutex> lg(mu_);

     std::uint32_t result;
     if (freeListHead_ != 0) {
       result = freeListHead_;
       freeListHead_ = items_[freeListHead_].next;
     } else {
       result = firstUntouchedId_++;
     }
     items_[result].owner = owner;
     return result;
   }

   void free(std::uint32_t id) {
     mutex::LockGuard<mutex::Mutex> lg(mu_);
     items_[id].next = freeListHead_;
     freeListHead_ = id;
   }

   T* lookupOwner(std::uint32_t id) {
     return items_[id].owner;
   }

 private:
  union FreeNodeOrItem {
    std::uint32_t next;
    T* owner;
  };

  mutex::Mutex mu_;
  FreeNodeOrItem* items_;
  std::uint32_t freeListHead_;
  std::uint32_t firstUntouchedId_;
  std::uint32_t maxElements_;
};

} // namespace internal
} // namespace rseq
