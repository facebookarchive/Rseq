/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

namespace rseq {
namespace internal {

constexpr int kCachelineSize = 64;

template <typename T, bool alreadyCachelineAligned>
struct CachelinePaddedImpl;
template <typename T>
struct CachelinePaddedImpl<T, true> {
  T item;
};
template <typename T>
struct CachelinePaddedImpl<T, false> {
  T item;
  char padding[kCachelineSize - sizeof(T) % kCachelineSize];
};

template <typename T>
struct CachelinePadded {
  // Casting from the return value of get() back to a CachelinePadded<T> is
  // guaranteed to work if T is standard-layout.
  T* get() {
    return &paddedItem.item;
  }

  // Note: can't be private; this struct must remain standard-layout to get the
  // guarantee that we can cast back and forth between the item and this struct
  // (in particular, we need this for Code objects).
  CachelinePaddedImpl<T, sizeof(T) % kCachelineSize == 0> paddedItem;
};

} // namespace internal
} // namespace rseq
