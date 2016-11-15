/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#if defined(__GNUC__) && __GNUC__ >= 4
#define RSEQ_LIKELY(x)   (__builtin_expect((x), 1))
#define RSEQ_UNLIKELY(x) (__builtin_expect((x), 0))
#else
#define RSEQ_LIKELY(x)   (x)
#define RSEQ_UNLIKELY(x) (x)
#endif
