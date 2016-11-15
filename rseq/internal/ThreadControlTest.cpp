/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/ThreadControl.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

#include "rseq/internal/Code.h"
#include "rseq/internal/NumCpus.h"
#include "rseq/internal/SwitchToCpu.h"

using namespace rseq::internal;

class ThreadControlFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    me = ThreadControl::get(&myThreadCachedCpu);

    childDead = false;
    childShouldDie = false;
    child = std::thread([&]() {
      std::unique_lock<std::mutex> ul(mu);
      while (true) {
        if (childShouldDie) {
          return;
        }
        if (func != nullptr) {
          func();
          func = nullptr;
        }
        cond.notify_all();
        cond.wait(ul, [&]() {
          return childShouldDie || func != nullptr;
        });
      }
    });

    childCpu = numCpus() > 1 ? 1 : 0;

    switchToCpu(0);
    // To check thread transition logic, we want the child to get its
    // ThreadControl on one CPU, and have it manipulated while it's on another.
    runOnChild([&]() {
      switchToCpu(0);
      childThreadControl = ThreadControl::get(&childThreadCachedCpu);
      switchToCpu(childCpu);
    });
  }

  void TearDown() override {
    if (!childDead) {
      killChild();
    }
    child.join();
  }

  // Only starts the child's death; doesn't join() it.
  void killChild() {
    std::lock_guard<std::mutex> lg(mu);
    childShouldDie = true;
    cond.notify_all();
  }

  void runOnChild(std::function<void ()> f) {
    func = f;
    std::unique_lock<std::mutex> ul(mu);
    cond.notify_all();
    cond.wait(ul, [&]() {
      return func == nullptr;
    });
  }

  std::atomic<int> myThreadCachedCpu;
  ThreadControl* me;

  int childCpu;
  std::atomic<int> childThreadCachedCpu;
  ThreadControl* childThreadControl;

  bool childDead;
  bool childShouldDie;
  std::thread child;
  std::mutex mu;
  std::condition_variable cond;
  std::function<void ()> func;
};

TEST_F(ThreadControlFixture, IdManipulation) {
  std::uint32_t myId = me->id();
  std::uint32_t childId = childThreadControl->id();
  EXPECT_EQ(me, ThreadControl::forId(myId));
  EXPECT_EQ(childThreadControl, ThreadControl::forId(childId));
}

TEST_F(ThreadControlFixture, Code) {
  Code* code = me->code();
  EXPECT_NE(nullptr, code->rseqLoadFunc());
  EXPECT_NE(nullptr, code->rseqStoreFunc());
  EXPECT_NE(nullptr, code->rseqStoreFenceFunc());
}

TEST_F(ThreadControlFixture, RseqManipulation) {
  std::uintptr_t dst = 0;
  runOnChild([&]() {
    EXPECT_FALSE(childThreadControl->code()->rseqStoreFunc()(&dst, 1));
  });
  EXPECT_EQ(1, dst);
  childThreadControl->blockRseqOps();
  childThreadCachedCpu.store(0);
  runOnChild([&]() {
    EXPECT_TRUE(childThreadControl->code()->rseqStoreFunc()(&dst, 2));
  });
  EXPECT_LT(childThreadCachedCpu.load(), 0);
  EXPECT_EQ(1, dst);
  childThreadControl->unblockRseqOps();
  runOnChild([&]() {
    EXPECT_FALSE(childThreadControl->code()->rseqStoreFunc()(&dst, 2));
  });
  EXPECT_EQ(2, dst);
}

TEST_F(ThreadControlFixture, CurCpu) {
  EXPECT_EQ(childCpu, childThreadControl->curCpu());
  runOnChild([&]() {
    switchToCpu(0);
  });
  EXPECT_EQ(0, childThreadControl->curCpu());
}

TEST_F(ThreadControlFixture, LivesWhileBeingAccessed) {
  me->accessing()->store(childThreadControl->id());
  killChild();
  /* sleep override */
  // Give it a bit to die on its own, if it's going to.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_EQ(childCpu, childThreadControl->curCpu());
  me->accessing()->store(0);
}

TEST_F(ThreadControlFixture, DiesWhenNotAccessed) {
  killChild();
  // If the child doesn't die, then we'll time out when the subsequent join()
  // call fails.
}
