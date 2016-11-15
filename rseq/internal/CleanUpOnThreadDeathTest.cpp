/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/CleanUpOnThreadDeath.h"

#include <pthread.h>

#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

using namespace rseq::internal;

int rseqVal;
int threadControlVal;
bool rseqValSetDuringThreadControlCleanup;

void rseqCleanupFunc() {
  rseqVal = 1;
}

void threadControlCleanupFunc() {
  rseqValSetDuringThreadControlCleanup = (rseqVal == 1);
  threadControlVal = 1;
}

TEST(CleanUpOnThreadDeath, CallsRseq) {
  rseqValSetDuringThreadControlCleanup = false;
  rseqVal = threadControlVal = 0;
  std::thread t([]() {
    setRseqCleanup(rseqCleanupFunc);
  });
  t.join();
  EXPECT_EQ(1, rseqVal);
}

TEST(CleanUpOnThreadDeath, CallsThreadControl) {
  rseqValSetDuringThreadControlCleanup = false;
  rseqVal = threadControlVal = 0;

  std::thread t([]() {
    setThreadControlCleanup(threadControlCleanupFunc);
  });
  t.join();
  EXPECT_EQ(1, threadControlVal);
}


TEST(CleanUpOnThreadDeath, OrdersCallsCorrectlyWhenAddedInOrder) {
  rseqValSetDuringThreadControlCleanup = false;
  rseqVal = threadControlVal = 0;
  std::thread t([]() {
    setRseqCleanup(rseqCleanupFunc);
    setThreadControlCleanup(threadControlCleanupFunc);
  });
  t.join();
  EXPECT_TRUE(rseqValSetDuringThreadControlCleanup);
}

TEST(CleanUpOnThreadDeath, OrdersCallsCorrectlyWhenNotAddedInOrder) {
  rseqValSetDuringThreadControlCleanup = false;
  rseqVal = threadControlVal = 0;
  std::thread t([]() {
    setThreadControlCleanup(threadControlCleanupFunc);
    setRseqCleanup(rseqCleanupFunc);
  });
  t.join();
  EXPECT_TRUE(rseqValSetDuringThreadControlCleanup);
}

TEST(CleanUpOnThreadDeath, OutlivesThreadLocals) {
  static __thread int deathCount;
  deathCount = 0;

  static void (*bumpAndCheckDeathCount)() = []() {
    EXPECT_EQ(0, deathCount);
    ++deathCount;
  };

  struct SetsCleanup {
    ~SetsCleanup() {
      setRseqCleanup(bumpAndCheckDeathCount);
    }
  };
  thread_local SetsCleanup setsCleanup1;
  thread_local SetsCleanup setsCleanup2;
  void* volatile odrUse = &setsCleanup1;
  odrUse = &setsCleanup2;
  setRseqCleanup(bumpAndCheckDeathCount);
}

TEST(CleanUpOnThreadDeath, SupportsReinitialization) {
  static pthread_key_t key1;
  static pthread_key_t key2;
  static pthread_key_t key3;

  struct TestInfo {
    TestInfo()
      : rseqInitialized(false),
        numRseqInitializations(0),
        numRseqDestructions(0) {}
    bool rseqInitialized;
    int numRseqInitializations;
    int numRseqDestructions;
  };
  // Note: only used by the child
  static __thread TestInfo* myTestInfo;
  std::unique_ptr<TestInfo> childTestInfo;

  static auto initializeRseq = []() {
    if (!myTestInfo->rseqInitialized) {
      myTestInfo->rseqInitialized = true;
      ++myTestInfo->numRseqInitializations;
      setRseqCleanup([]() {
        ++myTestInfo->numRseqDestructions;
        myTestInfo->rseqInitialized = false;
      });
    }
  };

  static void (*destructor3)(void*) = [](void*) {
    initializeRseq();
  };
  static void (*destructor1)(void*) = [](void*) {
    initializeRseq();
    pthread_setspecific(key3, reinterpret_cast<void*>(3));
  };
  static void (*destructor2)(void*) = [](void*) {
    initializeRseq();
  };
  static std::once_flag once;
  std::call_once(once, []() {
    pthread_key_create(&key1, destructor1);
    pthread_key_create(&key2, destructor2);
    pthread_key_create(&key3, destructor3);
  });


  std::thread t([&]() {
    // Easiest way to tell the pthread destructors where to find the TestInfo is
    // a threadlocal.
    myTestInfo = new TestInfo;
    childTestInfo.reset(myTestInfo);
    pthread_setspecific(key1, reinterpret_cast<void*>(1));
    initializeRseq();
    pthread_setspecific(key2, reinterpret_cast<void*>(2));
  });
  t.join();
  EXPECT_TRUE(
      childTestInfo->numRseqInitializations
          == childTestInfo->numRseqDestructions);
  EXPECT_FALSE(
      childTestInfo->rseqInitialized);
}
