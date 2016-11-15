# `Rseq.h`
--------

## Overview
***

This is a userspace take on the kernel restartable-sequences API. This allows
efficient per-cpu atomic operations that don't use barriers. A thread can
begin a restartable sequence (henceforth, "rseq"), and do rseq-load's and
rseq-stores. These are just like normal loads and stores (they're efficient
and don't come with any built-in barriers), which one exception: if another
thread has begun an rseq on the same CPU, then the load / store doesn't take
place, and returns an error code instead.

## History
***

This idea originated with "Fast mutual exclusion for uniprocessors"
(http://dl.acm.org/citation.cfm?id=143523), though similar ideas go back at
least to the 1980s, with "Concurrency Features for the Trellis/Owl Language"
(http://link.springer.com/chapter/10.1007%2F3-540-47891-4_16). "Mostly lock-free
malloc" (http://dl.acm.org/citation.cfm?id=512451) showed some impressive
performance wins by using essentially the same scheme. There has been a recent
resurgence in interest prompted by work done by Google
(http://www.linuxplumbersconf.org/2013/ocw/system/presentations/1695/original/LPC%20-%20PerCpu%20Atomics.pdf),
resulting in a number of attempts to provide support in the Linux kernel, the
most recent of which is at https://lkml.org/lkml/2016/8/19/699 .


## Usage example
***

To see why this is useful, let's consider a hypothetical malloc
implementation. At its core is a global data structure that keeps track of
chunks of free memory of various size classes, each size class organized into
a linked list.

Adding and removing elements from the centralized linked lists will be
expensive because of the synchronization overhead (lots of threads trying to
pull an element off the same linked list will get expensive). So in addition
to the centralized free-lists, we keep a per-thread cache.

Here's how the fast path alloc/free from a size-class might look then.
ThreadLocalSizeClassCache::head is the head of a linked-list based stack of
free memory.

    void free(void* memory) {
      ThreadLocalSizeClassCache* cache = myTLD()->sizeClassCacheForPtr(memory);
      *(void**) memory = cache->head;
      cache->head = memory;
    }

    void* alloc(size_t size) {
      ThreadLocalSizeClassCache* cache = myTLD()->sizeClassCacheForSize(size);
      if (cache->head == nullptr) {
        return getMemoryFromCentralFreeList(cache->sizeClass);
      }
      void* result = cache->head;
      cache->head = *(void**)cache->head;
      return result;
    }

But this approach has some problems. One big one is memory usage; to avoid
the locking overhead of the central free-lists, we need caches to be big. But
an N-byte cache per thread for T threads means we need N * T bytes reserved
in caches. It wouldn't be unrealistic for N to be on the order of millions
and T on the order of thousands. That's gigabytes of memory just sitting
around waiting to be used.

To save memory, we'll try per-CPU caching: make the linked-list stack where
we keep freed memory in a per-cpu data structure instead of a per-thread one.
Since there can be tens or even hundreds of threads per CPU, we may hope for
a dramatic reduction in memory sitting around unused in caches.

    void free(void* ptr) {
      while (true) {
        CpuLocalSizeClassCache* cache = myCLD()->sizeClassCacheForPtr(ptr);
        do {
          *(void**) ptr = cache->head;
        } while (!compareAndSwap(&cache->head, *(void**) ptr, ptr));
      }
    }

    void alloc(size_t size) {
      while (true) {
        CpuLocalSizeClassCache* cache = myCLD()->sizeClassCacheForSize(size);
        void* result = cache->head;
        if (result == nullptr) {
          return getMemoryFromCentralFreeList(cache->sizeClass);
        }
        void* newHead = *(void**) result;
        if (compareAndSwap(&cache->head, result, newHead)) {
          return result;
        }
      }
    }

There are two problems here:
1. We have a compare-and-swap on the fast paths for both allocation and free.
Even assuming cache hits, this is expensive.
2. There is an ABA problem in alloc. Resolving it involves strategies that
are complicated, error-prone, and slow.

Both of these problems are caused by the fact that a thread doesn't have any
way of knowing if another thread will run between the loading of cache->head
and the subsequent modification of it. This is exactly the problem that rseq
can solve.

Here's how it looks:

    void free(void* ptr) {
      while (true) {
        int cpu = rseq::begin();
        CpuLocalSizeClassCache* cache = cldFor(cpu)->sizeClassCacheForPtr(ptr);
        *(void**) ptr = cache->head;
        if (rseq::store(&cache->head, ptr)) {
          return;
        }
      }
    }

    void alloc(size_t size) {
      while (true) {
        int cpu = rseq::begin();
        CpuLocalSizeClassCache* cache = cldFor(cpu)->sizeClassCacheForSize(size);

        void* result = cache->head;
        if (result == nullptr) {
          return getMemoryFromCentralFreeList(cache->sizeClass);
        }
        void* newHead = *(void**) result;
        if (rseq::store(&cache->head, newHead)) {
          return result;
        }
      }
    }

This is efficient (an rseq store has very little overhead over a
plain-store), and correct (the store to cache->head will fail if another
thread touched the cpu-local data after the call to rseq::begin(), avoiding
the ABA-problem).


## Implementation
***

We'll cover rseq::store only; the other functions are similar. Each thread
gets its own copy of the following function:

    bool storeImpl(uint64_t* dst, uint64_t val) {
    do_store:
      *dst = val;
    success_path:
      return success;
    failure_path:
      return failure;
    }

That is to say, we dynamically generate the assembly for storeImpl once per
thread. Note that failure_path is unreachable as written.

Additionally, there is a global cache that maps cpu -> thread owning that
cpu, and a thread-local int that indicates the CPU a thread thinks it's
running on. In rseq::begin(), we see if globalCpuOwner[myCachedCpu] == me,
and if so, return myCachedCpu.

The interesting case is if we detect an ownership change. If that happens, we
update myCachedCpu, and look at globalCpuOwner[myCachedCpu] with the new
value of myCachedCpu. We're going to block that thread's stores. We do so by
patching the victim thread's copy of storeImpl to instead look like:

    bool storeImpl(uint64_t* dst, uint64_t val) {
    do_store:
      goto failure_path; // Store instruction has been overwritten with a jump!
    success_path:
      return success;
    failure_path:
      return failure;
    }

After this store becomes visible to the victim, we know that any victim rseqs
are done, and we may proceed; we cas ourselves into becoming the owner of the
CPU and are done.

The implementation is slightly more complicated; we need an
asymmetricThreadFence() to make sure the victim thread has made its
operations visible and seen the blocking of its operations. By looking at
/proc/self/task/<victim_tid>/stat, we can usually tell if the other thread
has been migrated or simply descheduled, and thereby usually avoid the fence.
As described, we have an ABA issue when a victim thread has its operations
blocked and re-enables them and runs again on the same CPU. We fix this by
having globalCpuOwner[n] store a <owner, curEvictor> pair rather than just
the owner.

Note that if we aren't able to prove that the previous thread running on this
CPU has been descheduled (say, because thread migrations are very frequent),
then we have to take a slow path involving an IPI (triggered by an mprotect)
more often. This can cause overheads on the order of microseconds per scheduling
quantum.


## Dangers
***

We break a few rules at several layers of the stack. These are described below.
To increase our confidence that this behavior won't manifest, we include some
stress tests (see `Readme.md` for more information on how to build and run
tests).

### The CPU
Our approach (patching a store to a jump without synchronization) is officially
disallowed by the Intel architecture manuals
(http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-manual-325462.pdf
section 8.1.3, "Handling Self- and Cross-Modifying Code"). As a practical
matter, no problems have appeared under our stress testing. The AMD manuals
(http://support.amd.com/TechDocs/24593.pdf section 7.6.1) guarantee that it
works. Reading between the lines a little bit, I think this is likely safe
(we're patching a single-micro-op instruction to another single-micro-op
instruction at a word-aligned boundary that is only ever jumped to). Windows
hot-patching points do something similar (patching a NOP to a jump instead of a
store), so hopefully Intel will be conservative with this sort of behavior.

An alternative approach would be to abuse the breakpoint mechanism (int3). We
install a sigtrap handler that checks if the breakpoint we hit was on our copy
of the store function, and if so moves the pc to the failure path. The evicting
thread sets the breakpoint on the victim's copy of store and does an
asymmetricThreadFenceHeavy(). This assumes that cross-modifying breakpoint
insertion is allowed. This isn't stated explicitly, but the assumption is used
in the Linux kernel, so Intel will have a harder time breaking it. A fancier
variant is the following:
  - Insert the breakpoint on the store.
  - asymmetricThreadFenceHeavy()
  - Change the rest of the bytes in the store to a jump.
  - asymmetricThreadFenceHeavy()
  - Change the breakpoint to the first byte of the jump.
This makes it far less likely that the victim will have to hit the breakpoint.
In either case, we can try to use the /proc/self/ check mentioned above to avoid
the asymmetricThreadFenceHeavy()s.

A completely safe but slower approach is to put each thread's copies of its
functions on a page specific to that thread. An evicting thread removes the
execute permissions of the victim thread's page to stop it, and the victim fixes
things up in a sigsegv handler.

The advantages of the current approach over the others are speed (no cross-core
activity on the fast path) and the fact that it does not need to steal a signal.


### The kernel

There are two issues here.

#### The mprotect hack
We assume that our asymmetricThreadFenceHeavy() call gets the effect of a
sys_membarrier() for cheap (i.e. without descheduling the calling thread). This
works for now, about which Linus says "I'd be a bit leery about it"
(https://lists.lttng.org/pipermail/lttng-dev/2015-March/024269.html).

#### Trusting `/proc/stat/task/<tid>/stat`
To avoid the cost of the asymmetricThreadFenceHeavy() down the fast path where
the victim has been descheduled rather than changed CPUs, we read its CPU out of
/proc and see that it's assigned to our CPU; we then know that it will see the
eviction. This works because the task's CPU is updated on the old CPU before it
changes CPUs and begins running. If the kernel changes this, we'll break.


### The compiler

We have a few bits of undefined behavior:

- We manipulate pointers via a uintptr_t, and reinterpret the manipulated
  address as a pointer.
- There are a few instances of what I think are strict aliasing violations (the
  code patching, rseq_repr_t, maybe elsewhere).
- We use volatile as a stand-in for real atomics in places where we need C99
  compatibility, and use heuristic arguments about compiler reorderings and the
  fact that we're only concerned with x86.
