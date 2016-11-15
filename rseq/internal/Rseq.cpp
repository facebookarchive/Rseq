/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Rseq.h"

#include <sched.h>

#include <atomic>
#include <cstdint>

#include "rseq/internal/AsymmetricThreadFence.h"
#include "rseq/internal/Code.h"
#include "rseq/internal/CleanUpOnThreadDeath.h"
#include "rseq/internal/CpuLocal.h"
#include "rseq/internal/Mutex.h"
#include "rseq/internal/NumCpus.h"
#include "rseq/internal/ThreadControl.h"

namespace rseq {
namespace internal {

static __thread int lastCpu;

static __thread ThreadControl* me;

// In at least some environments, alignof(std::atomic<T>) == 4 if
// alignof(T) == 4, even if sizeof(T) == 8; this won't work for us.
// We could alignas this struct, but I think the gold standard
// for "this is lock-free regardless of compiler and standard library choices"
// is still using an integral type. So instead of using
// std::atomic<OwnerAndEvictor>, we use AtomicOwnerAndEvictor below.
struct OwnerAndEvictor {
  std::uint32_t ownerId;
  std::uint32_t evictorId;
};

struct AtomicOwnerAndEvictor {
  AtomicOwnerAndEvictor() : repr(0) {
  }

  OwnerAndEvictor load() {
    OwnerAndEvictor result;
    std::uint64_t view = repr.load();
    result.ownerId = view >> 32;
    result.evictorId = view & 0xFFFFFFFFU;
    return result;
  }

  bool cas(OwnerAndEvictor expected, OwnerAndEvictor desired) {
    std::uint64_t expectedRepr
        = (static_cast<std::uint64_t>(expected.ownerId) << 32)
            | expected.evictorId;
    std::uint64_t desiredRepr
        = (static_cast<std::uint64_t>(desired.ownerId) << 32)
            | desired.evictorId;
    return repr.compare_exchange_strong(expectedRepr, desiredRepr);
  }

  std::atomic<std::uint64_t> repr;
};

// Initialized in ensureMyThreadControlInitialized below.
// PodWrapper since there are shutdown order issues that mean this can't ever be
// safely destroyed (dying threads access it).
static CpuLocal<AtomicOwnerAndEvictor>* ownerAndEvictor;
static char ownerAndEvictorStorage alignas(CpuLocal<AtomicOwnerAndEvictor>) [
    sizeof(*ownerAndEvictor)];

static int acquireCpuOwnership() {
  while (true) {
    lastCpu = sched_getcpu();
    threadCachedCpu()->store(lastCpu, std::memory_order_relaxed);

    OwnerAndEvictor curOwnerAndEvictor
      = ownerAndEvictor->forCpu(lastCpu)->load();
    if (curOwnerAndEvictor.ownerId == 0) {
      if (ownerAndEvictor->forCpu(lastCpu)->cas(
            curOwnerAndEvictor, { me->id(), 0 } )) {
        return lastCpu;
      } else {
        continue;
      }
    }

    me->accessing()->store(
        curOwnerAndEvictor.ownerId, std::memory_order_relaxed);
    if (!ownerAndEvictor->forCpu(lastCpu)->cas(
          curOwnerAndEvictor, { curOwnerAndEvictor.ownerId, me->id() })) {
      me->accessing()->store(0, std::memory_order_relaxed);
      continue;
    }
    // The CAS succeeded, so we installed ourself as the evictor.
    curOwnerAndEvictor.evictorId = me->id();

    ThreadControl* victim = ThreadControl::forId(curOwnerAndEvictor.ownerId);
    victim->blockRseqOps(); // A

    if (lastCpu != sched_getcpu()) { // B
      me->accessing()->store(0, std::memory_order_relaxed);
      continue;
    }

    // This is a little bit tricky; why don't we *always* need to do the
    // asymmetricThreadFencyHeavy()?
    // We did the stores blocking the victim's rseq ops above (A), and then
    // viewed ourselves to be running on CPU lastCpu (B). So the blocking stores
    // will be visible to all threads that run on CPU lastCpu in the future. If
    // we observe victim->curCpu() == lastCpu below, we know that the victim is
    // such a thread. So either the victim ran in between the blocking stores
    // and now (in which case it did a CAS to lastCpu's OwnerEvictor from
    // <victim, me> to <victim, 0>, so we'll retry below), or the victim hasn't
    // run yet, in which case we don't need the heavy fence.
    // This relies on the memory ordering guarantee of ThreadControl::curCpu()
    // (which itself relies on the way the kernel handles thread migrations).
    if (victim->curCpu() != lastCpu) {
      asymmetricThreadFenceHeavy();
    }

    me->accessing()->store(0, std::memory_order_relaxed);

    if (ownerAndEvictor->forCpu(lastCpu)->cas(
          curOwnerAndEvictor, { me->id(), 0 })) {
      return lastCpu;
    }
  }
}

static mutex::OnceFlag ownerAndEvictorOnceFlag;

static void ensureMyThreadControlInitialized() {
  if (me == nullptr) {
    me = ThreadControl::get(threadCachedCpu());
    rseq_load_trampoline = me->code()->rseqLoadFunc();
    rseq_store_trampoline = me->code()->rseqStoreFunc();
    rseq_store_fence_trampoline = me->code()->rseqStoreFenceFunc();
    setRseqCleanup([]() {
      end();
      // If rseq is shut-down at thread-death, then resurrected at thread-death,
      // we need to make sure we re-initialize our data structures.
      me = nullptr;
    });

    mutex::callOnce(ownerAndEvictorOnceFlag, []() {
      ownerAndEvictor
          = new (ownerAndEvictorStorage) CpuLocal<AtomicOwnerAndEvictor>;
    });
  }
}

int beginSlowPath() {
  ensureMyThreadControlInitialized();
  end();
  me->unblockRseqOps();
  return acquireCpuOwnership();
}

void end() {
  threadCachedCpu()->store(-1, std::memory_order_relaxed);
  while (true) {
    OwnerAndEvictor curOwnerAndEvictor
        = ownerAndEvictor->forCpu(lastCpu)->load();
    if (curOwnerAndEvictor.ownerId != me->id()) {
      break;
    }
    if (ownerAndEvictor->forCpu(lastCpu)->cas(curOwnerAndEvictor, { 0, 0 })) {
      break;
    }
  }
}

static void evictOwner(int shard) {
  OwnerAndEvictor curOwnerAndEvictor = ownerAndEvictor->forCpu(shard)->load();
  if (curOwnerAndEvictor.ownerId == 0) {
    return;
  }

  me->accessing()->store(curOwnerAndEvictor.ownerId);
  if (ownerAndEvictor->forCpu(shard)->load().ownerId
      != curOwnerAndEvictor.ownerId) {
    me->accessing()->store(0, std::memory_order_relaxed);
    return;
  }

  ThreadControl* victim = ThreadControl::forId(curOwnerAndEvictor.ownerId);
  victim->blockRseqOps();

  me->accessing()->store(0, std::memory_order_relaxed);
}

void fenceWith(int shard) {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  ensureMyThreadControlInitialized();
  evictOwner(shard);
  asymmetricThreadFenceHeavy();
}

void fence() {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  ensureMyThreadControlInitialized();
  for (int i = 0; i < numCpus(); ++i) {
    evictOwner(i);
  }
  asymmetricThreadFenceHeavy();
}

} // namespace internal
} // namespace rseq
