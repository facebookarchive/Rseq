/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/SwitchToCpu.h"

#include <thread>

#include <gtest/gtest.h>

#include "rseq/internal/NumCpus.h"

using namespace rseq::internal;

TEST(SwitchToCpu, SwitchesCpus) {
  for (int i = 0; i < numCpus(); ++i) {
    switchToCpu(i);
    EXPECT_EQ(i, sched_getcpu());
  }
}
