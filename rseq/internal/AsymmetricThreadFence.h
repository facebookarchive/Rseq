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

inline void asymmetricThreadFenceLight() {
  asm volatile("" : : : "memory");
}

// Throws std::runtime_error on failure.
void asymmetricThreadFenceHeavy();

} // namespace internal
} // namespace rseq
