/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

// This file only exists because CMake will complain about libraries with no
// .cpp files (i.e. header-only libraries). Explicitly listing such libraries in
// a CMakeLists.txt file isn't strictly necessary (header-only libraries should
// "just work"), but it helps make library inter-dependencies clear.

namespace rseq {
namespace internal {
namespace dummy {
inline void dummy() {}
}
}
}
