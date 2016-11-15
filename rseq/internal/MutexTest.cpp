/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Mutex.h"

#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace rseq::internal::mutex;

TEST(Mutex, ProvidesExclusion) {
  const int kNumThreads = 10;
  const int kIncrementsPerThread = 1000000;

  Mutex mu;
  mu.init();
  int x = 0;
  int y = 0;
  std::vector<std::thread> threads(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([&]() {
      for (int j = 0; j < kIncrementsPerThread; ++j) {
        LockGuard<Mutex> lg(mu);
        EXPECT_TRUE(x == y);
        ++x;
        ++y;
      }
    });
  }
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
  }
  EXPECT_TRUE(x == y);
  EXPECT_EQ(kNumThreads * kIncrementsPerThread, x);
}

TEST(CallOnce, SimpleCase) {
  int x = 0;
  OnceFlag once;
  once.init();
  callOnce(once, [&]() {
    ++x;
  });
  callOnce(once, [&]() {
    ++x;
  });
  EXPECT_EQ(1, x);
}

TEST(CallOnce, Racy) {
  const int kNumTrials = 10000;
  const int kNumThreads = 10;
  for (int i = 0; i < kNumTrials; ++i) {
    std::vector<std::thread> threads(kNumThreads);
    std::atomic<bool> ready(false);
    int x = 0;
    OnceFlag once;
    once.init();
    for (int j = 0; j < kNumThreads; ++j) {
      threads[j] = std::thread([&]() {
        while (!ready.load()) {
         // Spin until all threads have a chance to win the race.
        }
        callOnce(once, [&]() {
          ++x;
        });
        EXPECT_EQ(1, x);
      });
    }
    ready.store(true);
    for (int j = 0; j < kNumThreads; ++j) {
      threads[j].join();
    }
    EXPECT_EQ(1, x);
  }
}
