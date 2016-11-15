# Rseq
Rseq is a userspace take on the proposed kernel restartable sequences API, and
provides and mechanism to perform efficient per-cpu operations.

This isn't intended to be a long-running project. Instead, its goal is to allow
userland experiments with rseq (without having to recompile the kernel), to
collect data as to how useful it would be.

## Example
Here is a simple demonstration of how to use rseq and why it might be useful. A
more thorough explanation (together with history and some implementation notes)
can be found in `Rseq.md`.

    rseq::Value<int> counterByCpu[kNumCpus];
    void bumpCounter() {
      while (true) {
        int cpu = rseq::begin();
        // A plain atomic load. rseq::Value types are API-compatible with
        // std::atomic.
        int curValue = counterByCpu[cpu].load();
        // rseq::store takes no action and returns false if another thread might
        // have run on the same CPU after the call to rseq::begin(). Otherwise, the
        // store happens and the call returns true.
        bool success = rseq::store(&counterByCpu[cpu], curValue + 1);
        if (success) {
          break;
        }
      }
    }

## Requirements
Rseq only works on Linux and x86-64. Building requires CMake and a recent
version of clang or g++. Building the tests requires a gtest installation.

## Building Rseq
In this directory, run:
    mkdir build
    cd build
    # Include the former option to produce an optimized build, and the latter to
    # enable running tests.
    cmake [-DCMAKE_BUILD_TYPE=Release] [-Dtest=ON] [-DCMAKE_INSTALL_PREFIX=</path/to/install/dir>] ../
    make

    # Now we can take some of our binaries for a test drive

    # If you passed -Dtest=ON above, this will run all tests.
    make test
    # Run a benchmark of a variety of mechanisms for incrementing a set of
    # counters.
    ./rseq_benchmark all 8 10000000

## Installing Rseq
For the common case, you probably want:
    mkdir build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release ../
    sudo make install

You can then compile programs that `#include "rseq/Rseq.h"` with
`g++ myProgram.cpp -lrseq`.


## How Rseq works
See `Rseq.md` for a more thorough description. Essentially, each thread gets its
own copy of the code that does an rseq operation. When one thread wants to evict
another from ownership of a CPU, it patches that thread's copy of the function
to jump to a failure path instead of doing the operation.

## Full documentation
[`Rseq.md`](Rseq.md) contains a more thorough description. Reading the comments in
[`rseq/Rseq.h`](rseq/Rseq.h) should give a working understanding of the API.

## License
Rseq is BSD-licensed. We also provide an additional patent grant.
