# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

add_subdirectory(internal)
# internal/CMakeLists.txt populates the all_sources variable.
add_library(rseq ${all_sources})

rseq_gtest(
  rseq_test
  RseqTest.cpp
  rseq
  cpu_local
  num_cpus
  switch_to_cpu
)

rseq_gtest(
  rseq_c_test
  RseqCTest.cpp
  rseq
  cpu_local
  num_cpus
  switch_to_cpu
)

install (TARGETS rseq DESTINATION lib)
