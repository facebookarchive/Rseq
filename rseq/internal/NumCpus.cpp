/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/NumCpus.h"

namespace rseq {
namespace internal {

namespace detail {
mutex::OnceFlag numCpusOnceFlag;
} // namespace detail

} // namespace internal
} // namespace rseq
