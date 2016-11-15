# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

add_library(asymmetric_thread_fence AsymmetricThreadFence.cpp)
target_link_libraries(
  asymmetric_thread_fence
  mutex
  errors
)
list(APPEND all_sources internal/AsymmetricThreadFence.cpp)

rseq_gtest(
  asymmetric_thread_fence_test
  AsymmetricThreadFenceTest.cpp
  asymmetric_thread_fence
  num_cpus
)


add_library(cacheline_padded Dummy.cpp)

rseq_gtest(
  cacheline_padded_test
  CachelinePaddedTest.cpp
  cacheline_padded
)


add_library(clean_up_on_thread_death CleanUpOnThreadDeath.cpp)
target_link_libraries(
  clean_up_on_thread_death
  errors
  mutex
)
list(APPEND all_sources internal/CleanUpOnThreadDeath.cpp)

rseq_gtest(
  clean_up_on_thread_death_test
  CleanUpOnThreadDeathTest.cpp
  clean_up_on_thread_death
)


add_library(code Code.cpp)
target_link_libraries(code cacheline_padded mutex os_mem)
list(APPEND all_sources internal/Code.cpp)

rseq_gtest(
  code_test
  CodeTest.cpp
  code
)


add_library(cpu_local Dummy.cpp)
target_link_libraries(cpu_local cacheline_padded num_cpus os_mem)

rseq_gtest(
  cpu_local_test
  CpuLocalTest.cpp
  cpu_local
  num_cpus
  switch_to_cpu
)


add_library(id_allocator Dummy.cpp)
target_link_libraries(id_allocator mutex os_mem)

rseq_gtest(
  id_allocator
  IdAllocatorTest.cpp
  id_allocator
)

add_library(errors Errors.cpp)
list(APPEND all_sources internal/Errors.cpp)

rseq_gtest(
  errors_test
  ErrorsTest.cpp
  errors
)


add_library(intrusive_linked_list Dummy.cpp)

rseq_gtest(
  intrusive_linked_list_test
  IntrusiveLinkedListTest.cpp
  intrusive_linked_list
)


add_library(likely Dummy.cpp)
# LIKELY and UNLIKELY macros not tested


add_library(mutex Mutex.cpp)
target_link_libraries(mutex likely)
list(APPEND all_sources internal/Mutex.cpp)

rseq_gtest(
  mutex_test
  MutexTest.cpp
  mutex
)


add_library(num_cpus NumCpus.cpp)
list(APPEND all_sources internal/NumCpus.cpp)
target_link_libraries(num_cpus mutex)
# numCpus() not tested


add_library(os_mem OsMem.cpp)
target_link_libraries(
  os_mem
  errors
)
list(APPEND all_sources internal/OsMem.cpp)

rseq_gtest(
  os_mem_test
  OsMemTest.cpp
  os_mem
  errors
)


add_library(internal_rseq Rseq.cpp rseq_c.cpp rseq_c_inlines.c)
target_link_libraries(
  internal_rseq
  asymmetric_thread_fence
  code
  cpu_local
  errors
  mutex
  num_cpus
  thread_control
)
list(
  APPEND
  all_sources
  internal/Rseq.cpp
  internal/rseq_c.cpp
  internal/rseq_c_inlines.c
)
# rseq is tested through the public interface; no rseq_gtest here.


add_library(switch_to_cpu SwitchToCpu.cpp)
target_link_libraries(
  switch_to_cpu
  errors
)
# SwitchToCpu.cpp is test-only; we don't include it in all_sources
rseq_gtest(
  switch_to_cpu_test
  SwitchToCpuTest.cpp
  num_cpus
  switch_to_cpu
)


add_library(thread_control ThreadControl.cpp)
target_link_libraries(
  thread_control
  clean_up_on_thread_death
  code
  id_allocator
  intrusive_linked_list
  mutex
)
list(APPEND all_sources internal/ThreadControl.cpp)

rseq_gtest(
  thread_control_test
  ThreadControlTest.cpp
  num_cpus
  switch_to_cpu
  thread_control
)

set (all_sources ${all_sources} PARENT_SCOPE)
