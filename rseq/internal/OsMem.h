/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <cstdint>

namespace rseq {
namespace internal {
namespace os_mem {

// Allocation functions throw a std::runtime_exception on failure.
void* allocate(std::size_t bytes);
void* allocateExecutable(std::size_t bytes);
void free(void* ptr, std::size_t bytes);


} // namespace os_mem
} // namespace internal
} // namespace rseq
