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
#include <cstring>
#include <type_traits>

#include "rseq/internal/Likely.h"
#include "rseq/internal/Rseq.h"
#include "rseq/internal/rseq_c.h"

namespace rseq {

template <typename T>
class Value;

template <typename T>
bool load(T* dst, const Value<T>* src);

template <typename T, typename U>
bool store(Value<T>* dst, U&& val);

template <typename T, typename U>
bool storeFence(Value<T>* dst, U&& val);

// Overview
//
// This is a userspace take on the kernel restartable-sequences API. This allows
// efficient per-cpu atomic operations that don't use barriers. A thread can
// begin a restartable sequence (henceforth, "rseq"), and do rseq-load's and
// rseq-stores. These are just like normal loads and stores (they're efficient
// and don't come with any built-in barriers), which one exception: if another
// thread has begun an rseq on the same CPU, then the load / store doesn't take
// place, and returns an error code instead.
//
// See Rseq.md for a more thorough overview.

// Example
//
// It's well known that using CAS, one can implement an arbitrary fetch-and-phi
// operation (where 'phi' is any function from X -> X). When we want to do these
// operations per-cpu, rseq can result in dramatic speed-ups.
//
// Without rseq:
// std::atomic<int> data[kNumCpus];
//
// int fetchAndSquare() {
//   while (true) {
//     int cpu = sched_getcpu();
//     int cur = data[cpu].load(std::memory_order_relaxed);
//     if (data[cpu].compare_exchange_strong(cur, cur * cur)) {
//       return cur;
//     }
//   }
// }
//
// With rseq:
// rseq::Value<int> data[kNumCpus];
//
// int fetchAndSquare() {
//   while (true) {
//     int cpu = rseq::begin();
//     int cur = data[cpu].load(std::memory_order_relaxed);
//     if (rseq::store(&data[cpu], cur * cur)) {
//       return cur;
//     }
//   }
// }
//
// This does the same operation, and has about the same complexity, but the rseq
// version is significantly faster; it does a plain store instead of an
// expensive atomic operation.
//
// Rseq can also solve the other tricky issue with concurrent data structures
// built around CAS: the ABA problem. See Rseq.md for a more complete example.

// Caveats
// 1. The current implementation assumes x86-64 / TSO semantics (this isn't
// fundamental, but is something to keep in mind before trying to port to
// another architecture).
//
// 2. We only support types <= 8 bytes.
//
// 3. Down a slow-path, we may do an operation taking O(microseconds) (at most
// once a scheduling quantum). We try to avoid it, but can't make any
// guarantees.

// API and memory model specifics
//
// An rseq is started by a call to rseq::begin(). This returns an integer in
// [0, numCpus - 1], intended to be used as an index into per-cpu sharded data;
// the integer tells us which cpu's data we should use).
// The rseq lasts for an unspecified amount of time after the call. It might
// even terminate immediately after beginning; the length of an rseq is a QoI
// issue, not an API guarantee (we try very hard to ensure that an rseq lasts
// at least until the initiating thread gets descheduled).
//
// Rseqs started with the same rseq::begin() return value are totally ordered;
// the stores done in or visible to an rseq with shard index N are always
// visible to subsequent rseqs with shard index N. An rseq may end at any time
// (even spuriously; an rseq may end even if no other thread has begun an rseq
// since this one began). Therefore, a thread that reads some sharded data
// within an rseq should almost always ensure that the view it got was
// consistent, by checking that the rseq is still ongoing at some point after
// the reads are done.
//
// A warning on pointer-chasing:
// Rseqs have seqlock-like semantics. The data you read might not be consistent;
// the only way to be sure you saw a consistent view of things is if you find
// that the rseq is ongoing at some point after you read some data. Following a
// pointer is dangerous unless you're sure that the pointed-to data will still
// be alive even if you rseq has ended at the time of the read. This is done
// most easily by reading any unsafe data through rseq::load(). Note that you
// probably want to use RSEQ_MEMBER_ADDR if you do this.
//
// rseq::Value objects are API-compatabile with std::atomics, including the use
// of std::memory_orders (with the same semantics).
//
// RSEQ_MEMBER_ADDR macro:
// In general, we use rseq::load because we want to load a member from a struct
// whose existence we aren't sure about. But if we have a SomeType* someTypePtr,
// it's undefined behavior to do *anything at all* with it unless we know that
// the pointed-to memory has not been freed. This macro doesn't fix that, but it
// attempts to obscure the fact well enough to ensure that we don't actually let
// the compiler break us, and doesn't trigger an asan/ubsan/msan warnings. In
// particular, it never dereferences its argument, even purely syntactically.
//
// This pre-decays array fields. This is almost always what you want. Example:
//
// struct Foo {
//   float justSomeRandomData;
//   bool someOtherPieceOfData;
//   int arr[22];
// };
// Foo* foo;
// auto ptr = RSEQ_MEMBER_ADDR(foo, arr);
//
// Then "ptr" is of type int*, not int (*)[22]. Writing
// "&RSEQ_MEMBER_ADDR(foo, arr)[7]" gives a pointer to the 7th element of the
// arr field of the object foo points to. Note that we shouldn't ever write that
// though, since if we know foo is safe to dereference, we don't need this macro
// at all. Instead we want "RSEQ_MEMBER_ADDR(foo, arr) + 7". This gives
// a pointer to the element without dereferencing it.
//
// The macro preserves const and volatile qualifiers.
//
// "ptr" and "member" should be plain identifier names; advanced syntactic
// constructs like commas are not supported.
template <typename T>
struct ReferenceRemoveExtent;
template <typename T>
struct ReferenceRemoveExtent<T&> {
  typedef typename std::remove_extent<T>::type& type;
};
#define RSEQ_MEMBER_ADDR(ptr, member) \
    (&reinterpret_cast< \
        rseq::ReferenceRemoveExtent<decltype((ptr->member))>::type>( \
            *const_cast<char*>( \
                reinterpret_cast<const volatile char*>(ptr) \
                    + offsetof( \
                        std::remove_reference<decltype(*ptr)>::type, member))))


template <typename T>
class Value {
 public:
  static_assert(sizeof(std::atomic<T>) <= 8,
      "Can only have a Value<T> when T is <= 8 bytes and can be atomic!");

  Value() = default;
  explicit constexpr Value(T t) : repr_(toRepr(t)) {}
  Value(const Value&) = delete;

  Value& operator=(const Value&) = delete;

  T operator=(T t) {
    repr_ = toRepr(t);
    return t;
  }

  bool is_lock_free() const {
    return true;
  }

  static constexpr bool is_always_lock_free() {
    return true;
  }

  void store(T val, std::memory_order order = std::memory_order_seq_cst) {
    repr_.store(toRepr(val), order);
  }

  T load(std::memory_order order = std::memory_order_seq_cst) const {
    return fromRepr(repr_.load(order));
  }

  /* implicit */ operator T() const {
    return load();
  }

  T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) {
    return fromRepr(repr_.exchange(toRepr(desired), order));
  }

  bool compare_exchange_weak(
      T& expected, T desired,
      std::memory_order successOrder, std::memory_order failureOrder) {
    unsigned long expectedRepr = toRepr(expected);
    unsigned long desiredRepr = toRepr(desired);
    bool result = repr_.compare_exchange_weak(
        expectedRepr, desiredRepr, successOrder, failureOrder);
    expected = fromRepr(expectedRepr);
    return result;
  }

  bool compare_exchange_weak(
      T& expected, T desired,
      std::memory_order order = std::memory_order_seq_cst) {
    unsigned long expectedRepr = toRepr(expected);
    unsigned long desiredRepr = toRepr(desired);
    bool result = repr_.compare_exchange_weak(expectedRepr, desiredRepr, order);
    expected = fromRepr(expectedRepr);
    return result;
  }

  bool compare_exchange_strong(
      T& expected, T desired,
      std::memory_order successOrder, std::memory_order failureOrder) {
    unsigned long expectedRepr = toRepr(expected);
    unsigned long desiredRepr = toRepr(desired);
    bool result = repr_.compare_exchange_strong(
        expectedRepr, desiredRepr, successOrder, failureOrder);
    expected = fromRepr(expectedRepr);
    return result;
  }

  bool compare_exchange_strong(
      T& expected, T desired,
      std::memory_order order = std::memory_order_seq_cst) {
    unsigned long expectedRepr = toRepr(expected);
    unsigned long desiredRepr = toRepr(desired);
    bool result = repr_.compare_exchange_strong(
        expectedRepr, desiredRepr, order);
    expected = fromRepr(expectedRepr);
    return result;
  }

  // We don't implement the numeric operations. I think we could, but I'm not
  // knowledgeable enough about the numeric conversion rules to be sure (it's
  // tricky, because we would need to e.g. implement Value<int>::fetch_add in
  // terms of atomic<unsigned long>::fetch_add).
  // If you actually have a use case for them, we can figure it out then (I'm
  // already on the fence about allowing values of size other than 8, so that
  // would tip the scales).

 private:
  friend bool ::rseq::load<T>(T* dst, const Value<T>* src);
  // Can't do partial specialization of friend declarations; we just make store
  // with *any* types a friend.
  template <typename U, typename V>
  friend bool ::rseq::store(Value<U>* dst, V&& val);
  template <typename U, typename V>
  friend bool ::rseq::storeFence(Value<U>* dst, V&& val);

  // toRepr and fromRepr let us dodge aliasing violations and avoid dealing with
  // sizes.
  // Note that we static_assert using an std::atomic<T> above, so we know that T
  // is trivially copyable.
  static unsigned long toRepr(T t) {
    unsigned long result = 0;
    std::memcpy(&result, &t, sizeof(T));
    return result;
  }

  static T fromRepr(unsigned long repr) {
    T result;
    std::memcpy(&result, &repr, sizeof(T));
    return result;
  }

  unsigned long* raw() const {
    return reinterpret_cast<unsigned long*>(
        const_cast<std::atomic<unsigned long>*>(&repr_));
  }

  std::atomic<unsigned long> repr_;
};

// Returns a shard index. Ensures that any rseqs on other threads that received
// the same shard index are over before returning.
inline int begin() {
  int ret = internal::threadCachedCpu()->load();
  if (RSEQ_UNLIKELY(ret < 0)) {
    ret = internal::beginSlowPathWrapper();
  }
  return ret;
}

// Tries to do "*dst = *src;" in the rseq last started by this thread, with
// memory_order_seq_cst semantics.
// If this returns true, then the load was successful and the rseq was not yet
// over at the time of the load. (Note: the store to dst may take place after
// the rseq is over).
// If it returns false, then the rseq ended at some point prior to the call, and
// no load or store occurred.
// May only be called after begin().
// This is slighly slower than regular atomic loads, so those should be used
// unless the load being part of the rseq is required for correctness (e.g.
// pointer-chasing through dynamically allocated memory).
template <typename T>
bool load(T* dst, const Value<T>* src) {
  // An asymmetricThreadFenceLight() belongs after the load, but we omit it to
  // avoid namespace pollution. Invoking the generated code accomplishes the
  // same thing.
  if (sizeof(T) == 8) {
    unsigned long* realDst = reinterpret_cast<unsigned long*>(dst);
    return RSEQ_LIKELY(!rseq_load_trampoline(realDst, src->raw()));
  } else {
    unsigned long realDst;
    bool result = RSEQ_LIKELY(!rseq_load_trampoline(&realDst, src->raw()));
    if (result) {
      *dst = Value<T>::fromRepr(realDst);
    }
    return result;
  }
}

// Tries to do "*dst = val;" in the rseq last started by this thread, with
// memory_order_release semantics.
// If this function returns true, then the store was performed, and the rseq was
// not yet over at the time of the store.
// If it returns false, then the rseq ended at some point prior to the call, and
// no store occurred.
// May only be called after begin().
template <typename T, typename U>
bool store(Value<T>* dst, U&& val) {
  // Here as above we omit the asymmetricThreadFenceLight().
  return RSEQ_LIKELY(
      !rseq_store_trampoline(
          dst->raw(),
          Value<T>::toRepr(static_cast<decltype(val)&&>(val))));
}

// Tries to do "*dst = val;" in the rseq last started by this thread, with
// memory_order_seq_cst semantics.
// If this function returns true, then the store was performed, and the rseq was
// not yet over at the time of the store.
// If it returns false, then the rseq ended at some point prior to the call, and
// no store occurred.
// May only be called after begin().
template <typename T, typename U>
bool storeFence(Value<T>* dst, U&& val) {
  // Here as above we omit the asymmetricThreadFenceLight().
  return RSEQ_LIKELY(
      !rseq_store_fence_trampoline(
          dst->raw(),
          Value<T>::toRepr(static_cast<decltype(val)&&>(val))));
}

// If this returns true, then the rseq last started by this thread has not yet
// ended (and therefore, no other thread has called begin() and gotten back the
// same shard index as the calling thread after the calling thread).
inline bool validate() {
  Value<unsigned long> dummy;
  return store(&dummy, 0);
}

// Ends the current rseq.
// This does an atomic operation; in general it's better to just not do anything
// and wait until you hit a failure in an rseq operation.
// If you know you're likely to get descheduled soon (e.g. you're about to
// sleep), or that a thread on another CPU will try to acquire ownership of the
// current CPU (presumably while you do something else), then calling this first
// can speed up that thread's call to begin().
inline void end() {
  internal::endWrapper();
}

// Inserts a synchronization point in the rseq ordering of shard (ending the
// rseq prior to that point). Stores visible to rseqs on that shard before the
// point are visble to this thread after this function returns. Stores visible
// to this thread are visible to rseqs that occur after the point.
//
// This isn't really any faster that fence() in most cases. However:
// - Include fenceWith() makes the description of the memory model effects of
//   fence() simpler.
// - There are some optimizations we can apply that will make fenceWith() faster
//   than a plain fence().
inline void fenceWith(int shard) {
  internal::fenceWithWrapper(shard);
}

// Equivalent to, but faster than, a call to fenceWith each each possible
// argument.
inline void fence() {
  internal::fenceWrapper();
}

} // namespace rseq
