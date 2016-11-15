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

#include "rseq/internal/IntrusiveLinkedList.h"

namespace rseq {
namespace internal {

class Code;

class ThreadControl : public IntrusiveLinkedListNode<ThreadControl> {
 public:
  // Get the calling thread's ThreadControl.
  static ThreadControl* get(std::atomic<int>* threadCachedCpu);

  // Get the ThreadControl with the given id
  static ThreadControl* forId(std::uint32_t id);

  // Each living thread has a distinct id.
  std::uint32_t id() {
    return id_;
  }

  Code* code() {
    return code_;
  }

  // Block or unblock this thread's rseq operations.
  // This doesn't do any memory model trickery; it's up to callers to ensure
  // that this method's actions are visible to the victim before knowing that
  // no more rseq operations will happen / will succeed.
  void blockRseqOps();
  void unblockRseqOps();

  // Try to get the associated thread's current CPU (if its running), or else
  // the next CPU it will run on. May fail and return -1.
  // Memory ordering is tricky here. Everything is best effort, with the
  // exception of one memory ordering guarantee: a thread that observes itself
  // to be running on cpu N, and subsequently observes another thread to be
  // running on cpu N using curCpu, then the effect is that of an
  // asymmetricThreadFenceHeavy() that pairs only with an
  // asymmetricThreadFenceLight() in the other thread.
  int curCpu();

  // A ThreadControl object remains valid (and the corresponding thread alive)
  // whenever some other thread's accessing field contains its id, and when the
  // store happens-before the execution of die() below (which is executed when
  // the owning thread terminates).
  std::atomic<std::uint32_t>* accessing() {
    return &accessing_;
  }

 private:
  // We don't want users making their own ThreadControls; the semantics and
  // cleanup code need for each thread to have at most one ThreadControl.
  explicit ThreadControl(std::atomic<int>* threadCachedCpu);
  ~ThreadControl();

  Code* code_;
  int tid_;
  std::uint32_t id_;
  std::atomic<int>* threadCachedCpu_;
  std::atomic<std::uint32_t> accessing_;

  ThreadControl* next_;
  ThreadControl* prev_;
};

} // namespace internal
} // namespace rseq
