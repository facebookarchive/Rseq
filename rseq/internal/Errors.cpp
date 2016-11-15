/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Errors.h"

namespace rseq {
namespace internal {
namespace errors {

static __thread FatalErrorHandler curHandler;

void setFatalErrorHandler(FatalErrorHandler handler) {
  curHandler = handler;
}

FatalErrorHandler getFatalErrorHandler() {
  return curHandler;
}

void fatalError(const char* message) {
  curHandler(message);
}

} // namespace errors
} // namespace internal
} // namespace rseq
