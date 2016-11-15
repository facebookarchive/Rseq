/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/AsymmetricThreadFence.h"

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "rseq/internal/NumCpus.h"

using namespace rseq::internal;

class BiasedLock {
 public:
  BiasedLock()
    : fastTurn(true),
      fastInterested(false),
      slowInterested(false),
      slowMu(false) {
  }

  void lockFast() {
    fastInterested.store(true, std::memory_order_relaxed);
    fastTurn.store(true, std::memory_order_release);
    asymmetricThreadFenceLight();
    while (slowInterested.load() && fastTurn.load()) {
    }
  }

  void unlockFast() {
    fastInterested.store(false, std::memory_order_release);
  }

  void lockSlow() {
    bool expected;
    do {
      expected = false;
    } while (!slowMu.compare_exchange_weak(expected, true));
    slowInterested.store(true, std::memory_order_relaxed);
    fastTurn.store(false, std::memory_order_release);
    asymmetricThreadFenceHeavy();
    while(fastInterested.load() && !fastTurn.load()) {
    }
  }

  void unlockSlow() {
    slowInterested.store(false, std::memory_order_release);
    slowMu.store(false, std::memory_order_release);
  }

 private:
  std::atomic<bool> fastTurn;
  std::atomic<bool> fastInterested;
  std::atomic<bool> slowInterested;
  std::atomic<bool> slowMu;
};

TEST(AsymmetricThreadFence, BiasedLocking) {
  const std::uint64_t kFastIters = 3000000;
  const std::uint64_t kSlowIters = 10000;

  BiasedLock lock;
  std::uint64_t counter = 0;

  int numSlowThreads = numCpus() - 1;

  std::thread fastThread;
  std::vector<std::thread> slowThreads(numSlowThreads);

  // Start the slow threads incrementing the counter
  for (int i = 0; i < numSlowThreads; ++i) {
    slowThreads[i] = std::thread([&]() {
      for (int j = 0; j < kSlowIters; ++j) {
        lock.lockSlow();
        ++counter;
        lock.unlockSlow();
      }
    });
  }
  // Start the fast thread incrementing the counter
  fastThread = std::thread([&]() {
    for (int j = 0; j < kFastIters; ++j) {
      lock.lockFast();
      ++counter;
      lock.unlockFast();
    }
  });

  // Wait for the threads to finish.
  fastThread.join();
  for (int i = 0; i < numSlowThreads; ++i) {
    slowThreads[i].join();
  }
  EXPECT_EQ(kFastIters + numSlowThreads * kSlowIters, counter);
}
