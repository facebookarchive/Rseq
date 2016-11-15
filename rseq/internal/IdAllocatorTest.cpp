/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/IdAllocator.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

using namespace rseq::internal;

struct IdOwner {
  std::uint32_t id;
};

TEST(IdAllocator, SingleThreaded) {
  const int kNumOwners = 100000;

  // Remember that [] dereferencing automatically inserts the key with the value
  // 0.
  std::unordered_map<std::uint32_t, int> countForId;

  IdAllocator<IdOwner> idAllocator(kNumOwners + 1);

  std::vector<IdOwner> owners(kNumOwners);

  // Allocate a bunch of ids
  for (int i = 0; i < kNumOwners; ++i) {
    owners[i].id = idAllocator.allocate(&owners[i]);
    EXPECT_NE(0, owners[i].id);
    EXPECT_EQ(i + 1, owners[i].id);
    EXPECT_EQ(1, ++countForId[owners[i].id]);
  }

  // Check that the owners match up
  for (int i = 0; i < kNumOwners; ++i) {
    EXPECT_EQ(&owners[i], idAllocator.lookupOwner(owners[i].id));
  }

  // OK, now we mix things up a little

  // Free index i if i % 3 == 0
  for (int i = 0; i < kNumOwners; i += 3) {
    idAllocator.free(owners[i].id);
    EXPECT_EQ(0, --countForId[owners[i].id]);
  }

  // Free index i if i % 3 == 1
  for (int i = 1; i < kNumOwners; i += 3) {
    idAllocator.free(owners[i].id);
    EXPECT_EQ(0, --countForId[owners[i].id]);
  }

  // Allocate for index i if i % 3 == 0 or i % 3 == 1
  for (int i = 0; i < kNumOwners; ++i) {
    if (i % 3 == 0 || i % 3 == 1) {
      owners[i].id = idAllocator.allocate(&owners[i]);
      EXPECT_EQ(1, ++countForId[owners[i].id]);
      EXPECT_NE(0, owners[i].id);
    }
  }

  // Check that things still match
  for (int i = 0; i < kNumOwners; ++i) {
    EXPECT_EQ(&owners[i], idAllocator.lookupOwner(owners[i].id));
  }

  // At any given time, we had <= kNumOwners allocated Ids. Now we go to
  // kNumOwners + 1.
  IdOwner newOwner;
  newOwner.id = idAllocator.allocate(&newOwner);
  EXPECT_EQ(newOwner.id, kNumOwners + 1);
  EXPECT_EQ(1, ++countForId[newOwner.id]);
}

void updateMax(std::atomic<std::uint32_t>* max, std::uint32_t atLeast) {
  std::uint32_t curMax = max->load();
  while (curMax < atLeast) {
    if (max->compare_exchange_strong(curMax, atLeast)) {
      break;
    }
  }
}

TEST(IdAllocator, MultiThreaded) {
  const int kNumThreads = 10;
  const int kAllocationsPerThread = 100000;

  std::atomic<std::uint32_t> highestIdAllocated(0);

  std::vector<std::vector<IdOwner>> ownersByThread(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    ownersByThread[i] = std::vector<IdOwner>(kAllocationsPerThread);
  }

  IdAllocator<IdOwner> idAllocator(kNumThreads * kAllocationsPerThread + 1);

  std::vector<std::thread> threads(kNumThreads);

  // Spawn many threads, doing many allocations and frees
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([&, i]() {
      // Allocate everything
      for (int j = 0; j < kAllocationsPerThread; ++j) {
        ownersByThread[i][j].id = idAllocator.allocate(&ownersByThread[i][j]);
        EXPECT_NE(0, ownersByThread[i][j].id);
      }
      // Free the evens
      for (int j = 0; j < kAllocationsPerThread; j += 2) {
        idAllocator.free(ownersByThread[i][j].id);
      }
      // Reallocate them
      for (int j = 0; j < kAllocationsPerThread; j += 2) {
        ownersByThread[i][j].id = idAllocator.allocate(&ownersByThread[i][j]);
        EXPECT_NE(0, ownersByThread[i][j].id);
      }
    });
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }

  std::unordered_map<std::uint32_t, int> countForId;
  for (int i = 0; i < kNumThreads; ++i) {
    for (int j = 0; j < kAllocationsPerThread; ++j) {
      EXPECT_NE(0, ownersByThread[i][j].id);
      EXPECT_EQ(
          &ownersByThread[i][j],
          idAllocator.lookupOwner(ownersByThread[i][j].id));
    }
  }
  IdOwner newOwner;
  newOwner.id = idAllocator.allocate(&newOwner);
  EXPECT_EQ(kNumThreads * kAllocationsPerThread + 1, newOwner.id);
}
