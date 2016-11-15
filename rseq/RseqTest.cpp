/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/Rseq.h"

#include <sched.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "rseq/internal/CpuLocal.h"
#include "rseq/internal/SwitchToCpu.h"

TEST(RseqMemberAddr, GetsAddresses) {
  struct Type {
    int field1;
    char field2;
    float arrayField[17];
    double trailingField;
  };
  Type* t = new Type;
  EXPECT_EQ(&t->field1, RSEQ_MEMBER_ADDR(t, field1));
  EXPECT_EQ(&t->field2, RSEQ_MEMBER_ADDR(t, field2));
  EXPECT_EQ(&t->arrayField[0], RSEQ_MEMBER_ADDR(t, arrayField));
  EXPECT_EQ(&t->arrayField[0], &RSEQ_MEMBER_ADDR(t, arrayField)[0]);
  EXPECT_EQ(&t->arrayField[11], &RSEQ_MEMBER_ADDR(t, arrayField)[11]);
  EXPECT_EQ(&t->arrayField[11], RSEQ_MEMBER_ADDR(t, arrayField) + 11);
  EXPECT_EQ(&t->trailingField, RSEQ_MEMBER_ADDR(t, trailingField));
  delete t;
  // t is deleted; if sanitiizers are going to complain about these, they'll do
  // it now.
  void* volatile ignored;
  ignored = RSEQ_MEMBER_ADDR(t, field1);
  ignored = RSEQ_MEMBER_ADDR(t, field2);
  ignored = RSEQ_MEMBER_ADDR(t, arrayField) + 11;
  ignored = RSEQ_MEMBER_ADDR(t, trailingField);

  // Make sure that it even works with null.
  t = nullptr;
  ignored = RSEQ_MEMBER_ADDR(t, field2);

  // Silence warnings about an unused variable.
  (void) ignored;
}

TEST(RseqMemberAddr, PreservesQualifiers) {
  enum Qualification {
    kInvalid,
    kUnqualified,
    kConst,
    kVolatile,
    kConstVolatile,
  };
  struct DoesStores {
    void doStore(Qualification* qualification) {
      *qualification = kUnqualified;
    }
    void doStore(Qualification* qualification) const {
      *qualification = kConst;
    }
    void doStore(Qualification* qualification) volatile {
      *qualification = kVolatile;
    }
    void doStore(Qualification* qualification) const volatile {
      *qualification = kConstVolatile;
    }
  };

  struct Holder {
    DoesStores doesStores;
  };

  Holder holder;
  Holder* unqualifiedHolder = &holder;
  const Holder* constHolder = &holder;
  volatile Holder* volatileHolder = &holder;
  const volatile Holder* constVolatileHolder = &holder;

  Qualification qualification = kInvalid;

  RSEQ_MEMBER_ADDR(unqualifiedHolder, doesStores)->doStore(&qualification);
  EXPECT_EQ(kUnqualified, qualification);

  RSEQ_MEMBER_ADDR(constHolder, doesStores)->doStore(&qualification);
  EXPECT_EQ(kConst, qualification);

  RSEQ_MEMBER_ADDR(volatileHolder, doesStores)->doStore(&qualification);
  EXPECT_EQ(kVolatile, qualification);

  RSEQ_MEMBER_ADDR(constVolatileHolder, doesStores)->doStore(&qualification);
  EXPECT_EQ(kConstVolatile, qualification);
}

// It's hard to verify that the atomics actually act atomic; we just make sure
// the things that ought to compile do.
TEST(RseqValue, ActsLikeAtomic) {
  rseq::Value<int> i0;
  rseq::Value<int> i1(1);
  rseq::Value<int> i2{1};
  rseq::Value<double> d;

  rseq::Value<short> s;
  short s2 = s = 1;
  s.store(1);
  s.store(1, std::memory_order_relaxed);
  EXPECT_EQ(1, s.load());
  EXPECT_EQ(1, s.load(std::memory_order_acquire));
  EXPECT_EQ(1, s.exchange(2));
  EXPECT_EQ(2, s.load());
  EXPECT_EQ(2, s.exchange(2, std::memory_order_relaxed));
  short expected = 1;
  EXPECT_FALSE(s.compare_exchange_weak(expected, 3));
  EXPECT_EQ(2, expected);
  EXPECT_TRUE(s.compare_exchange_weak(expected, 3));
  s.compare_exchange_weak(expected, 0, std::memory_order_relaxed);
  s.compare_exchange_weak(
      expected, 0, std::memory_order_relaxed, std::memory_order_relaxed);
  expected = 1;
  s.store(2);
  EXPECT_FALSE(s.compare_exchange_strong(expected, 3));
  EXPECT_EQ(2, expected);
  EXPECT_TRUE(s.compare_exchange_strong(expected, 3));
  s.compare_exchange_strong(expected, 0, std::memory_order_relaxed);
  s.compare_exchange_strong(
      expected, 0, std::memory_order_relaxed, std::memory_order_relaxed);
}

TEST(Rseq, StoresCorrectly) {
  std::uint64_t threadsPerCore = 200;
  std::uint64_t incrementsPerThread = 1000000;
  std::uint64_t numCores = rseq::internal::numCpus();
  std::uint64_t numThreads = threadsPerCore * numCores;

  rseq::internal::CpuLocal<rseq::Value<std::uint64_t>> counters;
  for (int i = 0; i < numCores; ++i) {
    *counters.forCpu(i) = 0;
  }
  std::vector<std::thread> threads(numThreads);
  for (int i = 0; i < numThreads; ++i) {
    threads[i] = std::thread([&]() {
      for (int j = 0; j < incrementsPerThread; ++j) {
        while (true) {
          int cpu = rseq::begin();
          rseq::Value<std::uint64_t>* target = counters.forCpu(cpu);
          if (rseq::store(target, target->load() + 1)) {
            break;
          }
        }
      }
    });
  }
  for (int i = 0; i < numThreads; ++i) {
    threads[i].join();
  }
  std::uint64_t sum = 0;
  for (int i = 0; i < numCores; ++i) {
    sum += *counters.forCpu(i);
  }
  EXPECT_EQ(numThreads * incrementsPerThread, sum);
}

TEST(Rseq, StoreFencesCorrectly) {
  // First test that it does a store.
  rseq::Value<int> dst(0);
  /* int cpu = */ rseq::begin();
  EXPECT_TRUE(rseq::store(&dst, 1));
  EXPECT_EQ(1, dst.load());

  // Can't test fencing with only one processor.
  if (rseq::internal::numCpus() < 2) {
    return;
  }
  // We test fencing with dekker locking. The protected data is the counter
  // below.
  const int kIncrementsPerThread = 10000000;
  std::uint64_t counter1 = 0;
  std::uint64_t counter2 = 0;
  alignas(64) rseq::Value<int> turn;
  alignas(64) std::atomic<bool> interested0;
  alignas(64) std::atomic<bool> interested1;
  std::atomic<bool>* interested[] = {&interested0, &interested1};

  std::thread threads[2];
  for (int i = 0; i < 2; ++i) {
    threads[i] = std::thread([&, i]() {
      rseq::internal::switchToCpu(i);
      for (int j = 0; j < kIncrementsPerThread; ++j) {
        EXPECT_EQ(i, rseq::begin());
        interested[i]->store(true, std::memory_order_relaxed);
        EXPECT_TRUE(rseq::storeFence(&turn, 1 - i));
        while (interested[1 - i]->load() && turn.load() != i) {
          // spin
        }
        EXPECT_TRUE(counter1 == counter2);
        ++counter1;
        ++counter2;
        interested[i]->store(false, std::memory_order_release);
      }
    });
  }
  for (int i = 0; i < 2; ++i) {
    threads[i].join();
  }
  EXPECT_EQ(2 * kIncrementsPerThread, counter1);
  EXPECT_EQ(2 * kIncrementsPerThread, counter2);
}

TEST(Rseq, LoadsCorrectly) {
  int numThreads = 10;
  int rseqsPerThread = 100;

  rseq::Value<std::uint64_t> value(0);
  std::atomic<int> numThreadsAlive(numThreads);
  std::vector<std::thread> threads(numThreads);
  for (int i = 0; i < numThreads; ++i) {
    threads[i] = std::thread([&, i]() {
      rseq::internal::switchToCpu(0);
      for (int j = 0; j < rseqsPerThread; ++j) {
        int cpu = rseq::begin();
        EXPECT_EQ(0, cpu);
        if (!rseq::store(&value, i)) {
          continue;
        }
        while (true) {
          if (numThreadsAlive.load() == 1) {
            break;
          }
          std::uint64_t loadedValue = numThreads + 1;
          if (!rseq::load(&loadedValue, &value)) {
            EXPECT_EQ(numThreads + 1, loadedValue);
            break;
          }
          EXPECT_EQ(i, loadedValue);
        }
      }
      numThreadsAlive.fetch_sub(1);
    });
  }
  for (int i = 0; i < numThreads; ++i) {
    threads[i].join();
  }
}

TEST(Rseq, EndsCorrectly) {
  // A call to end() has no observable behavior; we test to make sure that it
  // won't cause crashes, but not much else.
  int numThreads = 100;
  int incrementsPerRseq = 100;
  int numRseqs = 10000;
  std::vector<std::thread> threads(numThreads);

  rseq::Value<std::uint64_t> counter(0);
  std::atomic<std::uint64_t> atomicCounter(0);

  for (int i = 0; i < numThreads; ++i) {
    threads[i] = std::thread([&]() {
      std::uint64_t localCounter = 0;
      rseq::internal::switchToCpu(0);
      for (int j = 0; j < numRseqs; ++j) {
        int cpu = rseq::begin();
        EXPECT_EQ(0, cpu);
        for (int k = 0; k < incrementsPerRseq; ++k) {
          std::uint64_t view = counter.load();
          bool success = rseq::store(&counter, view + 1);
          if (!success) {
            break;
          }
          ++localCounter;
        }
        rseq::end();
      }
      atomicCounter.fetch_add(localCounter);
    });
  }
  for (int i = 0; i < numThreads; ++i) {
    threads[i].join();
  }
  EXPECT_EQ(atomicCounter.load(), counter.load());
}

// Very dumb implementation based on spinning, but its enough to test the
// fencing primitives.
class RWLock {
 public:
  // If fenceWith is positive, we fence with that cpu. If it's -1, we fence with
  // *all* CPUs.
  explicit RWLock(int fenceWith)
    : readersMayBegin_(true),
      fenceWith_(fenceWith) {
    for (int i = 0; i < rseq::internal::numCpus(); ++i) {
      readerCounts_.forCpu(i)->store(0);
    }
  }

  void lock() {
    while (!readersMayBegin_.exchange(false)) {
    }
    if (fenceWith_ == -1) {
      rseq::fence();
    } else {
      rseq::fenceWith(fenceWith_);
    }
    std::int64_t sum;
    do {
      sum = 0;
      for (int i = 0; i < rseq::internal::numCpus(); ++i) {
        sum += readerCounts_.forCpu(i)->load();
      }
    } while (sum != 0);
  }

  void unlock() {
    readersMayBegin_.store(true);
  }

  void lock_shared() {
    while (true) {
      int cpu = rseq::begin();
      if (!readersMayBegin_.load()) {
        continue;
      }
      std::int64_t curCount = readerCounts_.forCpu(cpu)->load();
      if (rseq::store(readerCounts_.forCpu(cpu), curCount + 1)) {
        break;
      }
    }
  }

  void unlock_shared() {
    while (true) {
      int cpu = rseq::begin();
      std::int64_t curCount = readerCounts_.forCpu(cpu)->load();
      if (rseq::store(readerCounts_.forCpu(cpu), curCount - 1)) {
        break;
      }
    }
  }

 private:
  std::atomic<bool> readersMayBegin_;
  rseq::internal::CpuLocal<rseq::Value<std::int64_t>> readerCounts_;
  int fenceWith_;
};

void runFenceTest(
    int numReaders,
    int numReadLocks,
    int numWriteLocks,
    bool tieReadersToSameCpu) {
  rseq::internal::switchToCpu(0);
  int fenceWith;
  if (tieReadersToSameCpu) {
    fenceWith = rseq::internal::numCpus() > 1 ? 1 : 0;
  } else {
    fenceWith = -1;
  }

  RWLock lock(fenceWith);
  std::uint64_t val1 = 0;
  std::uint64_t val2 = 0;

  std::vector<std::thread> threads(numReaders);

  for (int i = 0; i < numReaders; ++i) {
    threads[i] = std::thread([&, i]() {
      if (tieReadersToSameCpu) {
        rseq::internal::switchToCpu(fenceWith);
      } else {
        rseq::internal::switchToCpu(i % rseq::internal::numCpus());
      }

      for (int j = 0; j < numReadLocks; ++j) {
        lock.lock_shared();
        EXPECT_TRUE(val1 == val2);
        lock.unlock_shared();
      }
    });
  }
  for (int i = 0; i < numWriteLocks; ++i) {
    lock.lock();
    EXPECT_TRUE(val1 == val2);
    ++val1;
    ++val2;
    lock.unlock();
  }
  for (int i = 0; i < numReaders; ++i) {
    threads[i].join();
  }
}

TEST(Rseq, FenceWithsCorrectly) {
  runFenceTest(10, 100000, 10000000, true);
}

TEST(Rseq, FencesCorrectly) {
  runFenceTest(40, 10000, 100000, false);
}

TEST(Rseq, ReinitializesCorrectly) {
  static pthread_key_t key1;
  static pthread_key_t key2;
  static pthread_key_t key3;
  static std::once_flag once;
  static void (*destructor3)(void*) = [](void*) {
    rseq::begin();
  };
  static void (*destructor1)(void*) = [](void*) {
    rseq::begin();
    pthread_setspecific(key3, reinterpret_cast<void*>(3));
  };
  static void (*destructor2)(void*) = [](void*) {
    rseq::begin();
  };

  std::call_once(once, []() {
    pthread_key_create(&key1, destructor1);
    pthread_key_create(&key2, destructor2);
    pthread_key_create(&key3, destructor3);
  });
  std::thread t([&]() {
    pthread_setspecific(key1, reinterpret_cast<void*>(1));
    rseq::begin();
    pthread_setspecific(key2, reinterpret_cast<void*>(2));
  });
  t.join();
}
