/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

/*

`./rseq_benchmark` for usage.

The output of lscpu on my machine:
Architecture:          x86_64
CPU op-mode(s):        32-bit, 64-bit
Byte Order:            Little Endian
CPU(s):                32
On-line CPU(s) list:   0-31
Thread(s) per core:    2
Core(s) per socket:    8
Socket(s):             2
NUMA node(s):          2
Vendor ID:             GenuineIntel
CPU family:            6
Model:                 45
Model name:            Intel(R) Xeon(R) CPU E5-2660 0 @ 2.20GHz
Stepping:              6
CPU MHz:               2201.000
CPU max MHz:           2201.0000
CPU min MHz:           1200.0000
BogoMIPS:              4405.46
Virtualization:        VT-x
L1d cache:             32K
L1i cache:             32K
L2 cache:              256K
L3 cache:              20480K
NUMA node0 CPU(s):     0-7,16-23
NUMA node1 CPU(s):     8-15,24-31
Flags:                 fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc aperfmperf eagerfpu pni pclmulqdq dtes64 monitor ds_cpl vmx smx est tm2 ssse3 cx16 xtpr pdcm pcid dca sse4_1 sse4_2 x2apic popcnt tsc_deadline_timer aes xsave avx lahf_lm ida arat epb pln pts dtherm tpr_shadow vnmi flexpriority ept vpid xsaveopt


According to some rough benchmarks:
When there are lots of threads
 - Counter increments using rseq stores are about 36% slower than ones using
   stack variables.
 - Counter increments using rseq stores are about 4.2x faster than ones using
   per-cpu atomics.
When there is only one thread:
 - Counter increments using rseq stores are about 9.8% slower than ones using
   stack variables.
 - Counter increments using rseq stores are about 5.3x faster than ones using
   per-cpu atomics.


The output of `./rseq_benchmark threadLocal,rseq,atomicsCachedCpu 256 100000000`:
===========================================================
Benchmarking Thread-local operations only (no sharing)
Increments: 25600000000
Seconds: 2.739452
TSC ticks: 6026707360
Single-CPU TSC ticks per increment: 0.235418
Global TSC ticks per increment: 7.533384
===========================================================
===========================================================
Benchmarking Per-cpu restartable sequences
Increments: 25600000000
Seconds: 3.732481
TSC ticks: 8211339968
Single-CPU TSC ticks per increment: 0.320755
Global TSC ticks per increment: 10.264175
===========================================================
===========================================================
Benchmarking Per-cpu atomics (with cached sched_getcpu calls)
Increments: 25600000000
Seconds: 15.678768
TSC ticks: 34492797698
Single-CPU TSC ticks per increment: 1.347375
Global TSC ticks per increment: 43.115997
===========================================================


The output of `./rseq_benchmark threadLocal,rseq,atomicsCachedCpu 1 100000000`:
===========================================================
Benchmarking Thread-local operations only (no sharing)
Increments: 100000000
Seconds: 0.255986
TSC ticks: 563156988
Single-CPU TSC ticks per increment: 5.631570
Global TSC ticks per increment: 180.210236
===========================================================
===========================================================
Benchmarking Per-cpu restartable sequences
Increments: 100000000
Seconds: 0.281085
TSC ticks: 618375013
Single-CPU TSC ticks per increment: 6.183750
Global TSC ticks per increment: 197.880004
===========================================================
===========================================================
Benchmarking Per-cpu atomics (with cached sched_getcpu calls)
Increments: 100000000
Seconds: 1.478343
TSC ticks: 3252272957
Single-CPU TSC ticks per increment: 32.522730
Global TSC ticks per increment: 1040.727346
===========================================================
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "rseq/Rseq.h"
#include "rseq/internal/NumCpus.h"

constexpr int kCachelineSize = 128;

struct PercpuCounter {
  std::atomic<std::uint64_t> atomicCounter;
  rseq::Value<std::uint64_t> rseqCounter;
  std::mutex mu;
  char padding[
      kCachelineSize
          - sizeof(atomicCounter)
          - sizeof(rseqCounter)
          - sizeof(mu)];
};

std::vector<PercpuCounter> counterByCpu;
char padding1[kCachelineSize - sizeof(counterByCpu)];

std::mutex contendedMu;
char padding2[kCachelineSize - sizeof(contendedMu)];

std::atomic<std::uint64_t> contendedCounter;

enum TestType {
  kLongCriticalSection,
  kContendedAtomics,
  kContendedLocks,
  kRseq,
  kAtomics,
  kAtomicsCachedCpu,
  kLocks,
  kLocksCachedCpu,
  kThreadLocal,
  kTestTypeEnd,
};

const char* testTypeString(TestType testType) {
  switch (testType) {
    case kLongCriticalSection:
        return "Long critical section";
    case kContendedAtomics:
        return "Contended atomics";
    case kContendedLocks:
        return "Contended locks";
    case kRseq:
        return "Per-cpu restartable sequences";
    case kAtomics:
        return "Per-cpu atomics";
    case kAtomicsCachedCpu:
        return "Per-cpu atomics (with cached sched_getcpu calls)";
    case kLocks:
        return "Per-cpu locks";
    case kLocksCachedCpu:
        return "Per-cpu locks (with cached sched_getcpu calls)";
    case kThreadLocal:
        return "Thread-local operations only (no sharing)";
    case kTestTypeEnd:
        /* should never happen */
        return nullptr;
  }
  return nullptr;
}

void doIncrementsLongCriticalSection(std::uint64_t numIncrements) {
  std::lock_guard<std::mutex> lg(contendedMu);
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    contendedCounter.store(contendedCounter.load(std::memory_order_relaxed) + 1,
                           std::memory_order_relaxed);
  }
}

void doIncrementsContendedAtomics(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    std::uint64_t old = contendedCounter.load();
    while (!contendedCounter.compare_exchange_weak(old, old + 1)) {
    }
  }
}

void doIncrementsContendedLocks(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    std::lock_guard<std::mutex> lg(contendedMu);
    contendedCounter.store(contendedCounter.load(std::memory_order_relaxed) + 1,
                           std::memory_order_relaxed);
  }
}

void doIncrementsRseq(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    bool success = false;
    do {
      int cpu = rseq::begin();
      std::uint64_t curVal = counterByCpu[cpu].rseqCounter.load();
      success = rseq::store(&counterByCpu[cpu].rseqCounter, curVal + 1);
    } while (!success);
  }
}

void doIncrementsAtomics(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    std::uint64_t old;
    int cpu;
    do {
      cpu = sched_getcpu();
      old = counterByCpu[cpu].atomicCounter.load();
    } while (!counterByCpu[cpu].atomicCounter.compare_exchange_weak(
          old, old + 1));
  }
}

void doIncrementsAtomicsCachedCpu(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements;) {
    int cpu = sched_getcpu();
    for (int j = 0; j < 100 && i < numIncrements; ++i, ++j) {
      std::uint64_t old = counterByCpu[cpu].atomicCounter.load();
      if (!counterByCpu[cpu].atomicCounter.compare_exchange_weak(
            old, old + 1)) {
        break;
      }
    }
  }
}

void doIncrementsLocks(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    int cpu = sched_getcpu();
    std::lock_guard<std::mutex> lg(counterByCpu[cpu].mu);
    counterByCpu[cpu].atomicCounter.store(
        counterByCpu[cpu].atomicCounter.load(std::memory_order_relaxed) + 1,
        std::memory_order_relaxed);
  }
}

void doIncrementsLocksCachedCpu(std::uint64_t numIncrements) {
  for (std::uint64_t i = 0; i < numIncrements;) {
    int cpu = sched_getcpu();
    for (int j = 0; j < 100 && i < numIncrements; ++i, ++j) {
      std::lock_guard<std::mutex> lg(counterByCpu[cpu].mu);
      counterByCpu[cpu].atomicCounter.store(
          counterByCpu[cpu].atomicCounter.load(std::memory_order_relaxed) + 1,
          std::memory_order_relaxed);
    }
  }
}

void doIncrementsThreadLocal(std::uint64_t numIncrements) {
  volatile std::uint64_t counter = 0;
  for (std::uint64_t i = 0; i < numIncrements; ++i) {
    std::uint64_t oldVal = counter;
    counter = oldVal + 1;
  }
  counterByCpu[0].atomicCounter.fetch_add(counter);
}

void printErrorIfNotEqual(std::uint64_t expected, std::uint64_t actual) {
  if (expected != actual) {
    std::printf(
        "Error: actual increment count %lu "
        "does not match expected increment count %lu.\n",
        actual,
        expected);
  }
}

std::uint64_t rdtscp() {
  std::uint32_t ecx;
  std::uint64_t rax,rdx;
  asm volatile ( "rdtscp\n" : "=a" (rax), "=d" (rdx), "=c" (ecx) : : );
  return (rdx << 32) + rax;
}

void runTest(
    TestType testType,
    std::uint64_t numThreads,
    std::uint64_t numIncrements) {
  contendedCounter.store(0);
  for (unsigned i = 0; i < counterByCpu.size(); ++i) {
    counterByCpu[i].atomicCounter.store(0);
    counterByCpu[i].rseqCounter.store(0);
  }
  void (*benchmarkThreadFunc)(std::uint64_t) =
      testType == kLongCriticalSection ? doIncrementsLongCriticalSection :
      testType == kContendedAtomics ? doIncrementsContendedAtomics :
      testType == kContendedLocks ? doIncrementsContendedLocks :
      testType == kRseq ? doIncrementsRseq :
      testType == kAtomics ? doIncrementsAtomics :
      testType == kAtomicsCachedCpu ? doIncrementsAtomicsCachedCpu :
      testType == kLocks ? doIncrementsLocks :
      testType == kLocksCachedCpu ? doIncrementsLocksCachedCpu :
      testType == kThreadLocal ? doIncrementsThreadLocal :
      nullptr;
  std::printf("===========================================================\n");
  std::printf("Benchmarking %s\n", testTypeString(testType));
  auto beginTime = std::chrono::high_resolution_clock::now();
  std::uint64_t beginCycles = rdtscp();
  std::vector<std::thread> threads(numThreads);
  for (unsigned i = 0; i < numThreads; ++i) {
    threads[i] = std::thread(benchmarkThreadFunc, numIncrements);
  }
  for (unsigned i = 0; i < numThreads; ++i) {
    threads[i].join();
  }
  std::uint64_t endCycles = rdtscp();
  auto endTime = std::chrono::high_resolution_clock::now();
  std::uint64_t expectedIncrements = numThreads * numIncrements;
  std::uint64_t actualIncrements = contendedCounter.load();
  for (std::uint64_t i = 0; i < rseq::internal::numCpus(); ++i) {
    actualIncrements += counterByCpu[i].atomicCounter.load();
    actualIncrements += counterByCpu[i].rseqCounter.load();
  }
  printErrorIfNotEqual(expectedIncrements, actualIncrements);
  std::chrono::nanoseconds duration = endTime - beginTime;
  std::uint64_t ns = duration.count();
  std::uint64_t cycles = endCycles - beginCycles;
  double seconds = static_cast<double>(ns) / 1000000000.0;
  std::printf("Increments: %lu \n", actualIncrements);
  std::printf("Seconds: %f\n", seconds);
  std::printf("TSC ticks: %lu \n", cycles);
  double myCycles = static_cast<double>(cycles) / actualIncrements;
  std::printf("Single-CPU TSC ticks per increment: %f\n", myCycles);
  std::printf("Global TSC ticks per increment: %f\n",
      rseq::internal::numCpus() * myCycles);
  std::printf("===========================================================\n");
}

const char* usage = R"(Usage: %s benchmarks num_threads increments_per_thread
  Where 'benchmarks' is either 'all', or a comma-separated list containing the
  benchmarks to run:
    longCriticalSection:  Each thread acquires a single shared lock, does all
                          its increments, and releases the lock.

    contendedAtomics:     Each thread updates a global counter with a CAS.

    contendedLocks:       Each thread acquires and releases a global lock for
                          counter increment.

    rseq:                 Threads increment cpu-local counters using restartable
                          sequences.

    atomics:              Threads increment cpu-local counters using CASs.

    atomicsCachedCpu:     Threads increment cpu-local counters using CASs, but
                          only call sched_getcpu once every 100 increments (or
                          until contention is detected).

    locks:                Threads increment cpu-local counters, protecting their
                          increments with locks.

    locksCachedCPu:       Threads increment cpu-local counters, protecting their
                          increments with locks, but only call sched_getcpu once
                          every 100 increments.

    threadLocal:          Threads increment thread-local counters, with no
                          synchronization.
)";

std::vector<TestType> parseBenchmarks(const char* benchmarks) {
  if (!strcmp(benchmarks, "all")) {
    return {
      kLongCriticalSection,
      kContendedAtomics,
      kContendedLocks,
      kRseq,
      kAtomics,
      kAtomicsCachedCpu,
      kLocks,
      kLocksCachedCpu,
      kThreadLocal
    };
  }

  std::vector<TestType> result;

  const char* benchmarksEnd = benchmarks + strlen(benchmarks);

  const char* tokBegin = benchmarks;
  while (true) {
    const char* tokEnd = std::strpbrk(tokBegin, ",");
    if (tokEnd == nullptr) {
      tokEnd = benchmarksEnd;
    }

    auto matches
        = [&](const char* str) { return std::equal(tokBegin, tokEnd, str); };

    TestType testType =
      matches("longCriticalSection") ? kLongCriticalSection :
      matches("contendedAtomics") ? kContendedAtomics :
      matches("contendedLocks") ? kContendedLocks :
      matches("rseq") ? kRseq :
      matches("atomics") ? kAtomics :
      matches("atomicsCachedCpu") ? kAtomicsCachedCpu :
      matches("locks") ? kLocks :
      matches("locksCachedCpu") ? kLocksCachedCpu :
      matches("threadLocal") ? kThreadLocal :
      kTestTypeEnd;

    if (testType == kTestTypeEnd) {
      std::printf(
          "Error: unknown benchmark type at the beginning of \"%s\"\n",
          tokBegin);
      std::exit(1);
    }
    result.push_back(testType);

    if (tokEnd == benchmarksEnd) {
      break;
    }
    tokBegin = tokEnd + 1;
  }
  return result;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::printf(usage, argv[0]);
    std::exit(1);
  }

  std::uint64_t numThreads;
  std::uint64_t numIncrements;

  std::vector<TestType> benchmarks = parseBenchmarks(argv[1]);

  numThreads = atol(argv[2]);
  numIncrements = atol(argv[3]);

  if (numThreads == 0 || numIncrements == 0) {
    std::printf("Error: invalid value for threads or increments\n");
    std::exit(1);
  }

  // PercpuCounter objects aren't moveable, so we construct a vector then swap
  // it with the global one.
  std::vector<PercpuCounter> p(rseq::internal::numCpus());
  counterByCpu.swap(p);

  for (TestType benchmark : benchmarks) {
    runTest(benchmark, numThreads, numIncrements);
  }

  return 0;
}
