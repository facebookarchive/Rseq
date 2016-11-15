/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Code.h"

#include <gtest/gtest.h>

using namespace rseq::internal;

TEST(Code, Allocation) {
  // Note: we assume that this is divisible by 4 later.
  const int kNumAllocations = 10000;
  std::atomic<int> threadCachedCpu[kNumAllocations];
  Code* code[kNumAllocations];
  for (int i = 0; i < kNumAllocations; ++i) {
    code[i] = Code::initForId(i, &threadCachedCpu[i]);
  }
  // Make sure they all work
  for (int i = 0; i < kNumAllocations; ++i) {
    std::uint64_t val = 12345;
    std::uint64_t dst = 0;
    EXPECT_FALSE(code[i]->rseqLoadFunc()(&dst, &val));
    EXPECT_EQ(12345, dst);
  }
  // Block the even ones
  for (int i = 0; i < kNumAllocations; i += 2) {
    code[i]->blockRseqOps();
  }
  // Make sure the evens don't work and the odds do.
  for (int i = 0; i < kNumAllocations; ++i) {
    std::uint64_t val = 12345;
    std::uint64_t dst = 0;
    EXPECT_EQ(i % 2 == 0, code[i]->rseqLoadFunc()(&dst, &val));
    EXPECT_EQ(12345 * (i % 2), dst);
  }
  // Block the odds
  for (int i = 1; i < kNumAllocations; i += 2) {
    code[i]->blockRseqOps();
  }
  // Reallocate the evens, but with a different mapping between threadCachedCpus
  // and Codes.
  for (int i = 0; i < kNumAllocations; ++i) {
    if (i % 4 == 0) {
      code[i] = Code::initForId(i / 2, &threadCachedCpu[i]);
    }
    if (i % 4 == 2) {
      // Here we use the knowledge that kNumAllocations is divisible by 4
      // (kNumAllocations / 2 is even).
      code[i] = Code::initForId(
          i / 2 + kNumAllocations / 2, &threadCachedCpu[i]);
    }
  }
  // Make sure the evens work and the odds dont.
  for (int i = 0; i < kNumAllocations; ++i) {
    std::uint64_t val = 12345;
    std::uint64_t dst = 0;

    bool failed = code[i]->rseqLoadFunc()(&dst, &val);
    if (i % 2 == 0) {
      EXPECT_FALSE(failed);
      EXPECT_EQ(12345, dst);
    }
  }
}

class CodeFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    code = Code::initForId(1, &threadCachedCpu);
    threadCachedCpu.store(0);
  }

  Code* code;
  std::atomic<int> threadCachedCpu;
};

TEST_F(CodeFixture, LoadsCorrectly) {
  std::uint64_t val = 12345;
  std::uint64_t dst = 0;
  EXPECT_FALSE(code->rseqLoadFunc()(&dst, &val));
  EXPECT_EQ(12345, dst);
  EXPECT_GE(0, threadCachedCpu.load());
}

TEST_F(CodeFixture, StoresCorrectly) {
  std::uint64_t dst = 0;
  EXPECT_FALSE(code->rseqStoreFunc()(&dst, 12345));
  EXPECT_EQ(12345, dst);
  EXPECT_GE(0, threadCachedCpu.load());
}

TEST_F(CodeFixture, StoreFencesCorrectly) {
  std::uint64_t dst = 0;
  EXPECT_FALSE(code->rseqStoreFenceFunc()(&dst, 12345));
  EXPECT_EQ(12345, dst);
  EXPECT_GE(0, threadCachedCpu.load());
}

TEST_F(CodeFixture, BlocksLoads) {
  std::uint64_t val = 12345;
  std::uint64_t dst = 0;
  code->blockRseqOps();
  EXPECT_TRUE(code->rseqLoadFunc()(&dst, &val));
  EXPECT_LT(threadCachedCpu.load(), 0);
  EXPECT_EQ(0, dst);
}

TEST_F(CodeFixture, BlocksStores) {
  std::uint64_t dst = 0;
  code->blockRseqOps();
  EXPECT_TRUE(code->rseqStoreFunc()(&dst, 12345));
  EXPECT_LT(threadCachedCpu.load(), 0);
  EXPECT_EQ(0, dst);
}

TEST_F(CodeFixture, BlocksStoreFences) {
  std::uint64_t dst = 0;
  code->blockRseqOps();
  EXPECT_TRUE(code->rseqStoreFenceFunc()(&dst, 12345));
  EXPECT_LT(threadCachedCpu.load(), 0);
  EXPECT_EQ(0, dst);
}

TEST_F(CodeFixture, UnblocksLoads) {
  std::uint64_t val = 12345;
  std::uint64_t dst = 0;
  code->blockRseqOps();
  code->unblockRseqOps();
  EXPECT_FALSE(code->rseqLoadFunc()(&dst, &val));
  EXPECT_EQ(12345, dst);
}

TEST_F(CodeFixture, UnblocksStores) {
  std::uint64_t dst = 0;
  code->blockRseqOps();
  code->unblockRseqOps();
  EXPECT_FALSE(code->rseqStoreFunc()(&dst, 12345));
  EXPECT_EQ(dst, 12345);
}

TEST_F(CodeFixture, UnblocksStoreFences) {
  std::uint64_t dst = 0;
  code->blockRseqOps();
  code->unblockRseqOps();
  EXPECT_FALSE(code->rseqStoreFenceFunc()(&dst, 12345));
  EXPECT_EQ(dst, 12345);
}
