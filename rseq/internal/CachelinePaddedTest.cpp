/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/CachelinePadded.h"

#include <gtest/gtest.h>

using namespace rseq::internal;

template <int dataSize>
struct SizedData {
  SizedData() {
    for (unsigned i = 0; i < dataSize; ++i) {
      data[i] = i;
    }
  }

  void doModifications() {
    for (unsigned i = 0; i < dataSize; ++i) {
      EXPECT_EQ(static_cast<unsigned char>(i), data[i]);
      ++data[i];
    }
  }

  ~SizedData() {
    for (unsigned i = 0; i < dataSize; ++i) {
      EXPECT_EQ(static_cast<unsigned char>(i + 1), data[i]);
    }
  }

  unsigned char data[dataSize];
};

using ExactlyCachelineSized = SizedData<kCachelineSize>;
using DoubleCachelineSized = SizedData<2 * kCachelineSize>;
using BelowCachelineSized = SizedData<kCachelineSize / 2>;
using AboveCachelineSized = SizedData<kCachelineSize + kCachelineSize / 2>;

TEST(CachelinePadded, Exact) {
  EXPECT_EQ(kCachelineSize, sizeof(CachelinePadded<ExactlyCachelineSized>));
  CachelinePadded<ExactlyCachelineSized> item;
  item.get()->doModifications();
  EXPECT_TRUE(reinterpret_cast<CachelinePadded<ExactlyCachelineSized>*>(
        item.get()) == &item);
}

TEST(CachelinePadded, Double) {
  EXPECT_EQ(2 * kCachelineSize, sizeof(CachelinePadded<DoubleCachelineSized>));
  CachelinePadded<DoubleCachelineSized> item;
  item.get()->doModifications();
  EXPECT_TRUE(reinterpret_cast<CachelinePadded<DoubleCachelineSized>*>(
        item.get()) == &item);
}

TEST(CachelinePadded, Below) {
  EXPECT_EQ(kCachelineSize, sizeof(CachelinePadded<BelowCachelineSized>));
  CachelinePadded<BelowCachelineSized> item;
  item.get()->doModifications();
  EXPECT_TRUE(reinterpret_cast<CachelinePadded<BelowCachelineSized>*>(
        item.get()) == &item);
}

TEST(CachelinePadded, Above) {
  EXPECT_EQ(2 * kCachelineSize, sizeof(CachelinePadded<AboveCachelineSized>));
  CachelinePadded<AboveCachelineSized> item;
  item.get()->doModifications();
  EXPECT_TRUE(reinterpret_cast<CachelinePadded<AboveCachelineSized>*>(
        item.get()) == &item);
}
