# SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# LoopNative's portable core: device tuning for the Galaxy Z Flip5, title profiles, the memory and
# thermal governors and the ExeFS (NSO) analyzer behind Prepare. It needs nothing from the
# emulator, so it also builds on its own for its tests:
#   cmake -S src/loopnative -B build/loopnative && cmake --build build/loopnative && ctest --test-dir build/loopnative

cmake_minimum_required(VERSION 3.22)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(loopnative CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(LOOPNATIVE_STANDALONE ON)
endif()

add_library(loopnative STATIC
    arm64_scan.cpp
    arm64_scan.h
    device.cpp
    device.h
    governor.cpp
    governor.h
    json_writer.h
    nso_analyzer.cpp
    nso_analyzer.h
    report.cpp
    report.h
    title_profiles.cpp
    title_profiles.h
)

target_include_directories(loopnative PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(loopnative PUBLIC cxx_std_20)

if (LOOPNATIVE_STANDALONE)
    if (NOT MSVC)
        target_compile_options(loopnative PRIVATE -Wall -Wextra -Wshadow -Wmissing-declarations -Wunused -Werror)
    endif()
    enable_testing()
    add_executable(loopnative_tests tests/loopnative_tests.cpp)
    target_link_libraries(loopnative_tests PRIVATE loopnative)
    add_test(NAME loopnative_tests COMMAND loopnative_tests)
endif()
