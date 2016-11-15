# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

cmake_minimum_required(VERSION 2.8)

project("Userspace restartable sequences")

option(test "Build all tests." OFF)
if (test)
  find_package(GTest REQUIRED)
  include_directories(${GTEST_INCLUDE_DIRS})

  enable_testing()
endif ()
function(rseq_gtest name src_file)
  if (test)
    add_executable(
      "${name}_runner"
      ${src_file}
    )
    target_link_libraries(
      "${name}_runner"
      ${GTEST_BOTH_LIBRARIES}
      ${ARGN}
    )
    add_test(
      NAME ${name}
      COMMAND "${name}_runner"
    )
  endif ()
endfunction(rseq_gtest)

set (CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11 -pthread -fno-exceptions")

include_directories(PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(rseq)

add_executable(rseq_benchmark RseqBenchmark.cpp)
target_link_libraries(rseq_benchmark rseq)

install(DIRECTORY rseq DESTINATION include FILES_MATCHING PATTERN "*.h")
