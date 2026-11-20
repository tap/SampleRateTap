# SPDX-License-Identifier: MIT
# Copyright 2026 Timothy Place and the SampleRateTap contributors
# Spike X-0: adds this directory's workloads (targets.cmake) to a root
# configure, so they build with exactly the targets, toolchain file and
# flags the ratchet uses. Passed by run_icount.py as
# -DCMAKE_PROJECT_SampleRateTap_INCLUDE=<this file>; the deferred include
# runs after the root CMakeLists.txt has defined tap::sr::rational. No CI
# configure passes it.
# EVAL expands the path now: a deferred call's arguments are expanded when it runs.
cmake_language(EVAL CODE
    "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL include \"${CMAKE_CURRENT_LIST_DIR}/targets.cmake\")")
