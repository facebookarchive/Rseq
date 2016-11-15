/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include "rseq/internal/Likely.h"

// A simple clone of parts of <mutex>. This lets us avoid depending on C++
// static constructors and linking against libstdc++ (which would stop people
// from linking with plain-C binaries).
// We only acquire mutexes down slow paths, so we don't bother doing anything
// fancy (like adaptive spinning, anything to avoid wasted wakeup attempts,
// etc.).

// These classes don't have constructors or destructors, so that they can live
// safely in static memory without any C++ runtime support. If they do in fact
// live in static memory, no initialization is needed. Otherwise, you have to
// call init() on them explicitly.

namespace rseq {
namespace internal {
namespace mutex {

template <typename Lock>
class LockGuard {
 public:
  explicit LockGuard(Lock& lock) : lock_(lock) {
    lock_.lock();
  }
  ~LockGuard() {
    lock_.unlock();
  }
 private:
  Lock& lock_;
};

class Mutex {
 public:
  void init() {
    state_.store(0, std::memory_order_relaxed);
  }

  void lock() {
    std::uint32_t oldState = state_.exchange(kHeldNoWaiter);
    if (oldState == kFree) {
      return;
    }
    oldState = state_.exchange(kHeldPossibleWaiter);
    while (oldState != kFree) {
      futexWait(kHeldPossibleWaiter);
      oldState = state_.exchange(kHeldPossibleWaiter);
    }
  }

  void unlock() {
    std::uint32_t oldState = state_.exchange(0);
    if (oldState == kHeldPossibleWaiter) {
      futexWake(1);
    }
  }

 private:
  constexpr static std::uint32_t kFree = 0;
  constexpr static std::uint32_t kHeldNoWaiter = 1;
  constexpr static std::uint32_t kHeldPossibleWaiter = 2;

  void futexWait(std::uint32_t val);
  void futexWake(int num);

  std::atomic<std::uint32_t> state_;
};


class OnceFlag;
template <typename Func, typename... Args>
void callOnce(OnceFlag&, Func&&, Args&&...);

class OnceFlag {
 public:
  void init() {
    initialized_.store(false, std::memory_order_relaxed);
    mu_.init();
  }
 private:
  template <typename Func, typename... Args>
  friend void ::rseq::internal::mutex::callOnce(OnceFlag&, Func&&, Args&&...);

  std::atomic<bool> initialized_;
  Mutex mu_;
};

template <typename Func, typename... Args>
void callOnce(OnceFlag& flag, Func&& func, Args&&... args) {
  if (RSEQ_LIKELY(flag.initialized_.load(std::memory_order_acquire))) {
    return;
  }
  LockGuard<Mutex> lg(flag.mu_);
  if (RSEQ_LIKELY(flag.initialized_.load(std::memory_order_relaxed))) {
    return;
  }
  func(std::forward<Args>(args)...);
  flag.initialized_.store(true, std::memory_order_release);
}

} // namespace mutex
} // namespace internal
} // namespace rseq
