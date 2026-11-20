# CMakeLists.txt: build of Protector of Earth Recomp.
#
# What is built:
#   1. PS2Recomp (thirdparty/PS2Recomp, pinned submodule): the ps2_recomp
#      recompiler and the ps2_runtime library.
#   2. The ben10_codegen target: runs ps2_recomp with ben10_poe.toml and
#      writes the game's C++ into generated/default/ (never versioned).
#   3. The ben10_poe executable: the generated C++ + the runtime + the port's
#      own code in src/. It only exists once generated/default/ has files:
#      after the first codegen, run configure again.
#   4. The tests (tests/, GoogleTest): they do not need the game.
#
# Full step by step: docs/01-building.md.

cmake_minimum_required(VERSION 3.21)
project(ProtectorOfEarthRecomp VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BEN10_BUILD_GAME  "Build the game executable (needs generated/default)" ON)
option(BEN10_BUILD_TESTS "Build the automated tests" ON)

# ---------------------------------------------------------------------------
# Minimum CPU
# ---------------------------------------------------------------------------
# The generated code and the runtime use SSE4.1 intrinsics (_mm_blendv_ps,
# _mm_min_epi32...) for the EE's 128-bit registers. MSVC accepts intrinsics
# without a flag; GCC and Clang need to know the CPU has SSE4.
# x86-64-v2 = SSE4.2 + POPCNT: any x86-64 CPU since ~2009. Same choice as
# Gitaroo Man Native. Without it, the first build stopped with
# "inlining failed ... target specific option mismatch" (findings/14 s.3).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(BEN10_ARCH_FLAGS "-march=x86-64-v2" CACHE STRING "CPU flags for GCC/Clang")
    add_compile_options(${BEN10_ARCH_FLAGS})
endif()

# ---------------------------------------------------------------------------
# PS2Recomp
# ---------------------------------------------------------------------------
# Only the recompiler and the runtime. The options below apply to the
# submodule; the values chosen here are documented in docs/01-building.md.
set(PS2X_BUILD_RECOMP   ON  CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME  ON  CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST     OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO   OFF CACHE BOOL "" FORCE)
# Per-function logs (PS2_FUNCTION_LOG_TRACKER) put a log call in every one of
# the ~20 thousand generated functions: much slower build and run.
# Off by default; turn it on to debug.
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "")
# PS2Recomp's ImGui debug panel: off until it is useful.
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "")
# MPEG video decoding through FFmpeg: the game links libipu
# (findings/09 s.7), but it is not known yet whether it has videos. Off
# until needed (so FFmpeg does not have to be installed).
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "")

# Our PS2Recomp fixes (patches/ps2recomp/, each with a header: what broke,
# why, the fix, how it was found). Applied here, in order, only if not
# applied yet: the submodule stays at the pinned commit and nobody has to
# apply anything by hand. To undo: git -C thirdparty/PS2Recomp checkout .
# (back to the original).
find_package(Git REQUIRED)
file(GLOB BEN10_PS2RECOMP_PATCHES "${CMAKE_SOURCE_DIR}/patches/ps2recomp/*.patch")
list(SORT BEN10_PS2RECOMP_PATCHES)
foreach(BEN10_PATCH IN LISTS BEN10_PS2RECOMP_PATCHES)
    get_filename_component(BEN10_PATCH_NAME "${BEN10_PATCH}" NAME)
    # If the patch reverse-applies cleanly, it is already applied.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${BEN10_PATCH}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp"
        RESULT_VARIABLE BEN10_PATCH_APPLIED OUTPUT_QUIET ERROR_QUIET)
    if(NOT BEN10_PATCH_APPLIED EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${BEN10_PATCH}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp"
            RESULT_VARIABLE BEN10_PATCH_RESULT ERROR_VARIABLE BEN10_PATCH_ERROR)
        if(NOT BEN10_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Patch ${BEN10_PATCH_NAME} did not apply: ${BEN10_PATCH_ERROR}\n"
                "Is the submodule at the right commit? (git submodule update --init)")
        endif()
        message(STATUS "PS2Recomp: applied ${BEN10_PATCH_NAME}")
    endif()
endforeach()

add_subdirectory(thirdparty/PS2Recomp)

# Optimise the runtime and the IOP library even in -O0 builds.
# The presets build the ~20 thousand generated files at -O0 so they compile
# in minutes. The runtime and the IOP code, though, do per-byte and
# per-cycle work: at -O0 one IOP disc read spent nearly all the run time in
# a per-byte ownership check (IopMemory::ownsRamRange), so a guest frame
# took many seconds and the boot looked frozen (findings/15 s.12).
# GCC/Clang only: with MSVC, /O2 cannot be combined with the Debug /RTC1
# flags; not done there yet.
if(NOT MSVC)
    foreach(BEN10_FAST_TARGET ps2_runtime ps2_iop)
        if(TARGET ${BEN10_FAST_TARGET})
            # -g keeps them debuggable: the boot work reads the EE
            # scheduler's thread table with gdb (findings/15 s.14).
            target_compile_options(${BEN10_FAST_TARGET} PRIVATE -O2 -g)
        endif()
    endforeach()
endif()

# PS2Recomp also defines the generic ps2EntryRunner executable (no game).
# We do not use it: it stays out of "build all".
if(TARGET ps2EntryRunner)
    set_target_properties(ps2EntryRunner PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ---------------------------------------------------------------------------
# Codegen: game ELF -> C++
# ---------------------------------------------------------------------------
add_custom_target(ben10_codegen
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/generated/default"
    COMMAND $<TARGET_FILE:ps2_recomp> "${CMAKE_SOURCE_DIR}/ben10_poe.toml"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS ps2_recomp
    COMMENT "Recompiling game/SLUS_216.61 into generated/default/"
    VERBATIM)

# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------
file(GLOB BEN10_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/generated/default/*.cpp")

if(BEN10_BUILD_GAME AND BEN10_GENERATED_SOURCES)
    list(LENGTH BEN10_GENERATED_SOURCES BEN10_GENERATED_COUNT)
    message(STATUS "ben10_poe: ${BEN10_GENERATED_COUNT} generated files")

    # main() is PS2Recomp's runner: it loads the ELF, sets up the runtime and
    # calls the recompiled entry point. The port will have its own in src/
    # when it needs one (window, options, saves).
    file(GLOB_RECURSE BEN10_PORT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

    add_executable(ben10_poe
        ${BEN10_GENERATED_SOURCES}
        ${BEN10_PORT_SOURCES}
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/src/main.cpp")

    target_include_directories(ben10_poe PRIVATE
        "${CMAKE_SOURCE_DIR}/generated/default"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(ben10_poe PRIVATE ps2_runtime)

    # Like ps2EntryRunner: merges ~32 generated files per compilation unit and
    # precompiles the heavy headers. Without it, each of the ~20 thousand
    # files reprocesses the same headers.
    set_target_properties(ben10_poe PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 32)
    target_precompile_headers(ben10_poe PRIVATE
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/include/ps2_runtime_macros.h"
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/include/ps2_runtime.h"
        "${CMAKE_SOURCE_DIR}/generated/default/ps2_recompiled_functions.h"
        "${CMAKE_SOURCE_DIR}/generated/default/ps2_recompiled_stubs.h"
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/include/ps2_syscalls.h"
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/include/ps2_stubs.h")

    if(MSVC)
        # Large generated functions exceed the COFF format's section limit.
        target_compile_options(ben10_poe PRIVATE /bigobj /FS)
    endif()
elseif(BEN10_BUILD_GAME)
    message(STATUS "ben10_poe: generated/default/ is empty. Build the ben10_codegen target and configure again.")
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BEN10_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
