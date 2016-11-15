/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

namespace rseq {
namespace internal {

// We want to centralize all the thread death logic for a couple reasons:
// - The order of execution matters; we do rseq cleanup before thread control
//   cleanup.
// - That's how jemalloc does it, so porting will be easier if we decide to.
// - The logic is actually a little bit subtle (there are ODR issues involved).
// We have to wrap up the calls behind a layer of indirection to avoid a
// circular dependency.
void setRseqCleanup(void (*)());
void setThreadControlCleanup(void (*)());

} // namespace internal
} // namespace rseq
