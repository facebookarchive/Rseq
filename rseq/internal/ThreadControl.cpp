/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/ThreadControl.h"

#include <sys/syscall.h>

#include <fcntl.h>
#include <unistd.h>
#include <sched.h>

#include <cstring>
#include <new>

#include "rseq/internal/CleanUpOnThreadDeath.h"
#include "rseq/internal/Code.h"
#include "rseq/internal/IdAllocator.h"
#include "rseq/internal/IntrusiveLinkedList.h"
#include "rseq/internal/Mutex.h"

namespace rseq {
namespace internal {

// ThreadControls are all kept in a global linked list. The list, including all
// additions to and removals from the list, are protected by the mutex.
// Theoretically we ought to worry about destructor order issues during
// shutdown, but as a practical matter everything works fine for these types.
static mutex::Mutex allThreadControlsMu;
static IntrusiveLinkedList<ThreadControl> allThreadControls;

// Initialized in ThreadControl::get below.
// Here we *do* care about destructors running during shutdown.
static mutex::OnceFlag idAllocatorOnceFlag;
static IdAllocator<ThreadControl>* idAllocator;
static char idAllocatorStorage alignas(IdAllocator<ThreadControl>) [
    sizeof(*idAllocator)];

// We get this from the kernel limit.
// TODO: Code.cpp has the same constant. We ought to move it into someplace
// common.
constexpr static int kMaxGlobalThreads = 1 << 22;

// The ThreadControl for the current thread. The rules around __thread variables
// in gcc are weird; putting ThreadControl directly in thread depends on a lot
// of finicky details. It's easier to do this lazy initialization hack.
static __thread ThreadControl* me;
static __thread char meStorage alignas(ThreadControl) [sizeof(*me)];

// static
ThreadControl* ThreadControl::get(std::atomic<int>* threadCachedCpu) {
  if (me != nullptr) {
    return me;
  }

  mutex::callOnce(idAllocatorOnceFlag, []() {
    idAllocator =
        new (idAllocatorStorage) IdAllocator<ThreadControl>(kMaxGlobalThreads);
  });

  me = new (meStorage) ThreadControl(threadCachedCpu);
  return me;
}

// static
ThreadControl* ThreadControl::forId(std::uint32_t id) {
  return idAllocator->lookupOwner(id);
}

ThreadControl::ThreadControl(std::atomic<int>* threadCachedCpu) {
  // Get our id.
  id_ = idAllocator->allocate(this);

  // Fill in the data about our process
  threadCachedCpu_ = threadCachedCpu;
  code_ = Code::initForId(id_, threadCachedCpu);
  tid_ = syscall(SYS_gettid);

  // Insert the ThreadControl into the global list
  {
    mutex::LockGuard<mutex::Mutex> lg(allThreadControlsMu);
    allThreadControls.link(this);
  }
  setThreadControlCleanup([]() {
    me->~ThreadControl();
    // If we're reinitialized during thread death, we need to *know* it, and
    // reinitialize our data structures.
    me = nullptr;
  });
}

ThreadControl::~ThreadControl() {
  // Remove ourselves from the list.
  {
    mutex::LockGuard<mutex::Mutex> lg(allThreadControlsMu);
    allThreadControls.unlink(this);
  }

  // Wait until no one's trying to evict us.
  bool beingAccessed = true;
  int numYields = 0;
  while (beingAccessed) {
    beingAccessed = false;
    {
      mutex::LockGuard<mutex::Mutex> lg(allThreadControlsMu);
      for (ThreadControl& thread : allThreadControls) {
        if (thread.accessing()->load() == id_) {
          beingAccessed = true;
          break;
        }
      }
    }
    if (!beingAccessed) {
      break;
    }
    // We yield for the first 100 attempts at dying. After that, we sleep.
    if (numYields < 100) {
      ++numYields;
      sched_yield();
    } else {
      /* sleep override */
      sleep(1);
    }
  }
  idAllocator->free(id_);
}

void ThreadControl::blockRseqOps() {
  threadCachedCpu_->store(-1, std::memory_order_relaxed);
  code_->blockRseqOps();
}

void ThreadControl::unblockRseqOps() {
  // threadCachedCpu is set at the point of the sched_getcpu() call.
  code_->unblockRseqOps();
}

// Returns -1 on error.
static int tryParseCpu(char* procFileContents, ssize_t length) {
  if (length < 0) {
    return -1;
  }

  int indexOfLastRParen = -1;
  for (int i = 0; i < length; ++i) {
    if (procFileContents[i] == ')') {
      indexOfLastRParen = i;
    }
  }
  if (indexOfLastRParen == -1) {
    return -1;
  }

  // Command is field 39, command is field 2.
  const int kSpacesBeforeCpu = 38;
  int pos = 0;
  for (
      int numSpacesEncountered = 0;
      pos < length && numSpacesEncountered < kSpacesBeforeCpu;
      ++pos) {
    if (procFileContents[pos] == ' ') {
      ++numSpacesEncountered;
    }
  }
  int cpu = 0;
  for (; pos < length; ++pos) {
    char charAtPos = procFileContents[pos];
    if (charAtPos == ' ') {
      return cpu;
    } else if ('0' <= charAtPos && charAtPos <= '9') {
      cpu *= 10;
      cpu += charAtPos - '0';
    } else {
      return -1;
    }
  }
  return -1;
}

// Returns a pointer to the first character after the integer output.
static char* rseqItoa(int i, char* a) {
  char* cur = a;
  if (i == 0) {
    *cur++ = '0';
  }
  while (i != 0) {
    *cur++ = '0' + i % 10;
    i /= 10;
  }
  // We printed the string least-significant digit first; we have to reverse it.
  for (char* left = a, *right = cur - 1; left < right; ++left, --right) {
    char temp = *right;
    *right = *left;
    *left = temp;
  }
  return cur;
}

int ThreadControl::curCpu() {
  // We want to construct "/proc/self/task/<tid>/stat".
  // "/proc/self/task/" is 16 characters, tid is a positive int, so it's at most
  // 10 characters. "/stat" is 5 characters, and we need 1 terminating null
  // character. Adding all these together, we get 32 characters.
  const int procFileNameSize = 32;
  // We know the types of all the fields in /proc/self/<tid>/stat, and can bound
  // their length to get the maximum buffer size we need, much the same way as
  // above. See P56392714 for the arithmetic.
  const int procFileContentsSize = 968;

  char filename[procFileNameSize];
  // What we want here is:
  //   snprintf(filename, sizeof(filename), "/prof/self/task/%d/stat", tid_);
  // But there are snprintf paths that can call malloc. Rather than try to
  // reason about the conditions under which this happens, we'll do our own
  // string printing.
  const char* filenamePrefix = "/proc/self/task/";
  const char* filenameSuffix = "/stat";
  std::strcpy(filename, filenamePrefix);
  char* tidStart = filename + std::strlen(filenamePrefix);
  char* suffixStart = rseqItoa(tid_, tidStart);
  std::strcpy(suffixStart, filenameSuffix);

  char procFileContents[procFileContentsSize];

  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    return -1;
  }
  // To get atomicity, we want to read the whole file (well, the part of it that
  // we care about anyway) in a single read() call. We retry in case a signal
  // causes a length of -1.
  ssize_t length = -1;
  for (int i = 0; i < 10 && length == -1; ++i) {
    length = read(fd, procFileContents, procFileContentsSize);
  }
  int cpu = tryParseCpu(procFileContents, length);
  close(fd);
  return cpu;
}

} // namespace internal
} // namespace rseq
