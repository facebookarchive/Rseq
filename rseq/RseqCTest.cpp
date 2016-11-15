/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/rseq_c.h"

#include <atomic>

#include <gtest/gtest.h>

// We don't put this through the ringer the same way we do the C++ interface. So
// long as it compiles and runs, we assume things are correct.
TEST(RseqC, SanityChecks) {
  rseq_repr_t rseqItem;
  reinterpret_cast<std::atomic<unsigned long>*>(&rseqItem)->store(1);
  rseq_value_t rseqValue;

  /* int cpu = */ rseq_begin();

  // Starts at 1
  EXPECT_TRUE(rseq_load(&rseqValue, &rseqItem));
  EXPECT_EQ(1, rseqValue);

  // Store 2, then load
  EXPECT_TRUE(rseq_store(&rseqItem, 2));
  EXPECT_TRUE(rseq_load(&rseqValue, &rseqItem));
  EXPECT_EQ(2, rseqValue);

  // Store-fence 3, then load
  EXPECT_TRUE(rseq_store_fence(&rseqItem, 3));
  EXPECT_TRUE(rseq_load(&rseqValue, &rseqItem));
  EXPECT_EQ(3, rseqValue);

  // Fence
  rseq_fence();

  // Store should fail then.
  EXPECT_FALSE(rseq_store(&rseqItem, 4));
  EXPECT_EQ(
      3, reinterpret_cast<std::atomic<unsigned long>*>(&rseqItem)->load());

  // Start up again
  /* int cpu = */ rseq_begin();

  // End
  rseq_end();

  // Start up yet again.
  /* int cpu = */ rseq_begin();

  // And things should work, even after ending.
  EXPECT_TRUE(rseq_store(&rseqItem, 5));
  EXPECT_TRUE(rseq_load(&rseqValue, &rseqItem));
  EXPECT_EQ(5, rseqValue);
}
