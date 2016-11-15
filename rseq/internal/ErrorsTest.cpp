/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/internal/Errors.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using namespace rseq::internal::errors;

TEST(Errors, DefaultThrows) {
  ThrowOnError thrower;
  std::string msg = "Some error message";
  bool exceptionCaught = false;
  try {
    fatalError(msg.c_str());
  } catch (const std::runtime_error& exception) {
    EXPECT_EQ(msg, exception.what());
    exceptionCaught = true;
  }
  EXPECT_TRUE(exceptionCaught);
}

TEST(Errors, AllowsChangingHandler) {
  ThrowOnError thrower;
  // Get a copy of the old handler.
  FatalErrorHandler oldHandler = getFatalErrorHandler();

  // Install a custom handler that throws a custom type.
  struct MyException {
  };
  FatalErrorHandler myHandler = +[](const char* /* message */) {
    throw MyException();
  };
  setFatalErrorHandler(myHandler);

  // Make sure the custom handler is called.
  bool exceptionCaught = false;
  try {
    fatalError("this gets ignored");
  } catch (const MyException& /* exception */) {
    exceptionCaught = true;
  }
  EXPECT_TRUE(exceptionCaught);

  // Make sure we can reinstall the old handler, and that it's the right one.
  setFatalErrorHandler(oldHandler);
  exceptionCaught = false;
  try {
    fatalError("this gets ignored too");
  } catch (const std::runtime_error& /* exception */) {
    exceptionCaught = true;
  }
  EXPECT_TRUE(exceptionCaught);
}

TEST(Errors, AbortOnErrorAborts) {
  AbortOnError aoe;
  ASSERT_DEATH(fatalError("ThisIsAnErrorString"), "ThisIsAnErrorString");
}

static void throwException() {
  throw std::runtime_error("Runtime error");
}

static void abortAfterCallingThrowException() {
  AbortOnError aoe;
  throwException();
}

static void tryCatchException() {
  try {
    abortAfterCallingThrowException();
  } catch (...) {
  }
}

TEST(Errors, AbortOnErrorAbortsAfterExceptions) {
  ASSERT_DEATH(tryCatchException(), "");
}

TEST(Errors, AbortOnErrorIsntPermanent) {
  ThrowOnError thrower;
  {
    AbortOnError aoe;
  }
  bool exceptionCaught = false;
  try {
    fatalError("blah blah blah");
  } catch (const std::runtime_error& /* exception */) {
    exceptionCaught = true;
  }
  EXPECT_TRUE(exceptionCaught);
}
