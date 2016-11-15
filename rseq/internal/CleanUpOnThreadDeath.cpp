/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */
#include "rseq/internal/CleanUpOnThreadDeath.h"

#include <pthread.h>

#include "rseq/internal/Mutex.h"
#include "rseq/internal/Errors.h"

namespace rseq {
namespace internal {

static __thread void (*cleanUpRseq)();
static __thread void (*cleanUpThreadControl)();

static __thread bool myDestructorScheduled;
static pthread_key_t pthreadOnceKey;
static mutex::OnceFlag destructorScheduledOnceFlag;

static void destructor(void* /* ignored */) {
  // If someone does an rseq operation *within* a pthread destructor, we'll
  // re-initialize our data.
  myDestructorScheduled = false;
  if (cleanUpRseq != nullptr) {
    cleanUpRseq();
  }
  if (cleanUpThreadControl != nullptr) {
    cleanUpThreadControl();
  }
  cleanUpRseq = nullptr;
  cleanUpThreadControl = nullptr;
}

static void ensureDestructorScheduled() {
  mutex::callOnce(destructorScheduledOnceFlag, []() {
    int err = pthread_key_create(&pthreadOnceKey, &destructor);
    if (err != 0) {
      errors::fatalError("Couldn't schedule thread death destructor");
    }
  });
  if (!myDestructorScheduled) {
    // Exists purely to schedule the destructor.
    pthread_setspecific(pthreadOnceKey, reinterpret_cast<void*>(1));
  }
}

void setRseqCleanup(void (*func)()) {
  cleanUpRseq = func;
  ensureDestructorScheduled();
}

void setThreadControlCleanup(void (*func)()) {
  cleanUpThreadControl = func;
  ensureDestructorScheduled();
}

} // namespace internal
} // namespace rseq
