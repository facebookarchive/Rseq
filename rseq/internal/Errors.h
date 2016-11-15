/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include <unistd.h>

#include <stdio.h>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>

namespace rseq {
namespace internal {
namespace errors {

namespace detail {
inline void abortWithMessage(const char* message) {
  // We ignore the error code; we can't do anything in case of a "real" failure,
  // and handling e.g. signal logic is more complicated than we need.
  write(STDERR_FILENO, message, std::strlen(message));
  std::abort();
}

inline void throwRuntimeException(const char* message) {
#if __EXCEPTIONS
  throw std::runtime_error(message);
#endif // __EXCEPTIONS
}
} // namespace detail

// This should not return; it should either terminate the program or throw an
// exception.
typedef void (*FatalErrorHandler)(const char* message);

// Error handlers are thread-local. The default one throws an
// std::runtime_exception
void setFatalErrorHandler(FatalErrorHandler handler);
FatalErrorHandler getFatalErrorHandler();

void fatalError(const char* message);

// While one of these is in scope, rseq failures will call abort(), and
// destruction via a thrown exception causes abort.
// Having the abort call happen implicitly in a destructor (as opposed to
// writing "catch(...) { abort(); }") is advantageous because it means that core
// dumps will show the stack trace of the function that threw the exception, not
// the one that caught it.
class AbortOnError {
 public:
  AbortOnError() {
    previousHandler_ = getFatalErrorHandler();
    setFatalErrorHandler(&detail::abortWithMessage);
  }
  ~AbortOnError() {
#if __EXCEPTIONS
    if (std::uncaught_exception()) {
      // Being destroyed as part of exception unwinding; abort.
      detail::abortWithMessage("Exception thrown into top-level C function.\n");
    }
#endif // __EXCEPTIONS
    setFatalErrorHandler(previousHandler_);
  }
  AbortOnError(const AbortOnError&) = delete;
  AbortOnError& operator=(const AbortOnError&) = delete;
 private:
  FatalErrorHandler previousHandler_;
};

#ifdef __EXCEPTIONS
class ThrowOnError {
 public:
  ThrowOnError() {
    previousHandler_ = getFatalErrorHandler();
    setFatalErrorHandler(&detail::throwRuntimeException);
  }
  ~ThrowOnError() {
    setFatalErrorHandler(previousHandler_);
  }
 private:
  FatalErrorHandler previousHandler_;
};
#else // __EXCEPTIONS
typedef AbortOnError ThrowOnError;
#endif // __EXCEPTIONS


} // namespace errors
} // namespace internal
} // namespace rseq
