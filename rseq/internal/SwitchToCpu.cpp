/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/SwitchToCpu.h"

#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <syscall.h>

#include <cstdlib>
#include <stdexcept>

#include "rseq/internal/Errors.h"

namespace rseq {
namespace internal {

void switchToCpu(int cpu) {
  pid_t tid = syscall(__NR_gettid);
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  int err = sched_setaffinity(tid, sizeof(cpu_set_t), &set);
  if (err != 0) {
    errors::fatalError("Couldn't switch cpus");
  }
}

} // namespace internal
} // namespace rseq
