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

// Switch to the given CPU. Throws a std::runtime_error if it couldn't do so
// successfully.
void switchToCpu(int cpu);

} // namespace internal
} // namespace rseq
