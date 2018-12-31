/**
 * Copyright (c) 2018-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "rseq/rseq_c.h"

extern inline int rseq_begin();
extern inline int rseq_load(rseq_value_t *dst, rseq_repr_t *src);
extern inline int rseq_store(rseq_repr_t *dst, rseq_value_t val);
extern inline int rseq_store_fence(rseq_repr_t *dst, rseq_value_t val);
extern inline int rseq_validate();
