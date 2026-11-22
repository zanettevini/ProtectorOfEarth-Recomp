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
# why, the fix, how it was found). Applied here, in order: the submodule
# stays at the pinned commit and nobody has to apply anything by hand.
#
# A stamp file in the submodule records the SHA-256 of the patch set last
# applied. When the set changes (a patch added, edited, merged or renamed),
# the submodule's files are reset to the pinned commit and every patch is
# applied again. (Checking each patch alone, as before, failed when a new
# patch changed the same lines as an older one, or replaced it.)
# Do not keep work in thirdparty/PS2Recomp itself: a reset removes it. Make
# it a patch (AGENTS.md s.5). To undo all patches by hand:
#   git -C thirdparty/PS2Recomp checkout . && git -C thirdparty/PS2Recomp clean -fd
find_package(Git REQUIRED)
set(BEN10_PS2RECOMP_DIR "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp")
set(BEN10_PATCH_STAMP "${BEN10_PS2RECOMP_DIR}/.ben10_patches_applied")
file(GLOB BEN10_PS2RECOMP_PATCHES "${CMAKE_SOURCE_DIR}/patches/ps2recomp/*.patch")
list(SORT BEN10_PS2RECOMP_PATCHES)
set(BEN10_PATCH_SET "")
foreach(BEN10_PATCH IN LISTS BEN10_PS2RECOMP_PATCHES)
    get_filename_component(BEN10_PATCH_NAME "${BEN10_PATCH}" NAME)
    file(SHA256 "${BEN10_PATCH}" BEN10_PATCH_HASH)
    string(APPEND BEN10_PATCH_SET "${BEN10_PATCH_NAME} ${BEN10_PATCH_HASH}\n")
endforeach()
string(SHA256 BEN10_PATCH_SET_HASH "${BEN10_PATCH_SET}")
set(BEN10_PATCH_STAMP_VALUE "")
if(EXISTS "${BEN10_PATCH_STAMP}")
    file(READ "${BEN10_PATCH_STAMP}" BEN10_PATCH_STAMP_VALUE)
    string(STRIP "${BEN10_PATCH_STAMP_VALUE}" BEN10_PATCH_STAMP_VALUE)
endif()
if(NOT BEN10_PATCH_STAMP_VALUE STREQUAL BEN10_PATCH_SET_HASH)
    message(STATUS "PS2Recomp: patch set changed, resetting the submodule's files and applying the patches")
    execute_process(COMMAND "${GIT_EXECUTABLE}" checkout -- .
        WORKING_DIRECTORY "${BEN10_PS2RECOMP_DIR}" RESULT_VARIABLE BEN10_RESET_RESULT)
    execute_process(COMMAND "${GIT_EXECUTABLE}" clean -fdq
        WORKING_DIRECTORY "${BEN10_PS2RECOMP_DIR}")
    if(NOT BEN10_RESET_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not reset thirdparty/PS2Recomp. Is the submodule there? (git submodule update --init)")
    endif()
    foreach(BEN10_PATCH IN LISTS BEN10_PS2RECOMP_PATCHES)
        get_filename_component(BEN10_PATCH_NAME "${BEN10_PATCH}" NAME)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply "${BEN10_PATCH}"
            WORKING_DIRECTORY "${BEN10_PS2RECOMP_DIR}"
            RESULT_VARIABLE BEN10_PATCH_RESULT ERROR_VARIABLE BEN10_PATCH_ERROR)
        if(NOT BEN10_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Patch ${BEN10_PATCH_NAME} did not apply: ${BEN10_PATCH_ERROR}\n"
                "Is the submodule at the right commit? (git submodule update --init)")
        endif()
        message(STATUS "PS2Recomp: applied ${BEN10_PATCH_NAME}")
    endforeach()
    file(WRITE "${BEN10_PATCH_STAMP}" "${BEN10_PATCH_SET_HASH}\n")
endif()

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

# ---------------------------------------------------------------------------
# Translated VU1 code (decision D14, findings/21)
# ---------------------------------------------------------------------------
# tools/vu1_translate reads the VU microprograms from the game's ELF and
# writes one C++ function per instruction pair into generated/vu1/ (the
# game's code: never versioned). ben10_vu1_native compiles them with the
# resolver that hands them to the VU1 interpreter (src/vu/vu1_native.h).
set(BEN10_GAME_ELF "${CMAKE_SOURCE_DIR}/game/SLUS_216.61")
set(BEN10_VU1_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated/vu1")
if(TARGET ps2_runtime AND EXISTS "${BEN10_GAME_ELF}")
    add_executable(ben10_vu1_translate EXCLUDE_FROM_ALL tools/vu1_translate/vu1_translate.cpp)
    target_link_libraries(ben10_vu1_translate PRIVATE ps2_runtime)

    set(BEN10_VU1_GENERATED_SOURCES "${BEN10_VU1_GENERATED_DIR}/vu1_native_index.cpp")
    # 32 files of pairs and 32 of blocks (findings/21 s.24), as kFileCount
    # in tools/vu1_translate/vu1_translate.cpp.
    foreach(BEN10_VU1_FILE RANGE 0 31)
        list(APPEND BEN10_VU1_GENERATED_SOURCES "${BEN10_VU1_GENERATED_DIR}/vu1_native_${BEN10_VU1_FILE}.cpp")
        list(APPEND BEN10_VU1_GENERATED_SOURCES "${BEN10_VU1_GENERATED_DIR}/vu1_native_blocks_${BEN10_VU1_FILE}.cpp")
    endforeach()
    add_custom_command(
        OUTPUT ${BEN10_VU1_GENERATED_SOURCES}
        COMMAND $<TARGET_FILE:ben10_vu1_translate> "${BEN10_GAME_ELF}" "${BEN10_VU1_GENERATED_DIR}"
        DEPENDS ben10_vu1_translate "${BEN10_GAME_ELF}"
        COMMENT "Translating the VU microprograms of game/SLUS_216.61 into generated/vu1/"
        VERBATIM)

    add_library(ben10_vu1_native STATIC
        ${BEN10_VU1_GENERATED_SOURCES}
        "${CMAKE_SOURCE_DIR}/src/vu/vu1_native_resolver.cpp")
    target_include_directories(ben10_vu1_native PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(ben10_vu1_native PUBLIC ps2_runtime)
    # Each translated file is heavy to optimise, and Ninja would otherwise
    # compile one per hardware thread at once: cap how many run together by
    # the machine's memory (BEN10_VU1_COMPILE_JOBS overrides; Ninja only).
    # Budget per file: 3 GB with MSVC, 1 GB with GCC/Clang (measured 0.7 GB
    # with GCC 13; findings/21 s.6).
    cmake_host_system_information(RESULT BEN10_HOST_MEMORY_MB QUERY TOTAL_PHYSICAL_MEMORY)
    if(MSVC)
        set(BEN10_VU1_FILE_BUDGET_MB 3072)
    else()
        set(BEN10_VU1_FILE_BUDGET_MB 1024)
    endif()
    math(EXPR BEN10_VU1_JOBS_DEFAULT "${BEN10_HOST_MEMORY_MB} / 2 / ${BEN10_VU1_FILE_BUDGET_MB}")
    if(BEN10_VU1_JOBS_DEFAULT LESS 1)
        set(BEN10_VU1_JOBS_DEFAULT 1)
    endif()
    set(BEN10_VU1_COMPILE_JOBS "${BEN10_VU1_JOBS_DEFAULT}" CACHE STRING
        "How many translated VU1 files may compile at once (default: half the RAM / budget per file)")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS ben10_vu1_compile=${BEN10_VU1_COMPILE_JOBS})
    set_property(TARGET ben10_vu1_native PROPERTY JOB_POOL_COMPILE ben10_vu1_compile)
    message(STATUS "Translated VU1 code: at most ${BEN10_VU1_COMPILE_JOBS} files compile at once (${BEN10_HOST_MEMORY_MB} MB RAM)")
    if(MSVC)
        target_compile_options(ben10_vu1_native PRIVATE /bigobj)
    else()
        # Optimised in every build, like the runtime (each pair's step must
        # be inlined and folded to be worth having).
        target_compile_options(ben10_vu1_native PRIVATE -O2 -g)
    endif()
endif()

# Offline replay of captured VU1 workloads (tools/vu1_replay, findings/20).
# Not part of "build all": build it with --target ben10_vu1_replay.
# --native runs them with the translated VU1 code (findings/21).
if(TARGET ps2_runtime)
    add_executable(ben10_vu1_replay EXCLUDE_FROM_ALL tools/vu1_replay/vu1_replay.cpp)
    target_link_libraries(ben10_vu1_replay PRIVATE ps2_runtime)
    if(TARGET ben10_vu1_native)
        target_link_libraries(ben10_vu1_replay PRIVATE ben10_vu1_native)
        target_compile_definitions(ben10_vu1_replay PRIVATE BEN10_HAVE_VU1_NATIVE=1)
    endif()
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
    # The resolver is part of ben10_vu1_native (shared with the replay tool).
    list(FILTER BEN10_PORT_SOURCES EXCLUDE REGEX "/src/vu/vu1_native_resolver\\.cpp$")
    if(NOT TARGET ben10_vu1_native)
        list(FILTER BEN10_PORT_SOURCES EXCLUDE REGEX "/src/vu/vu1_native_install\\.cpp$")
    endif()

    add_executable(ben10_poe
        ${BEN10_GENERATED_SOURCES}
        ${BEN10_PORT_SOURCES}
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/src/main.cpp")

    target_include_directories(ben10_poe PRIVATE
        "${CMAKE_SOURCE_DIR}/generated/default"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(ben10_poe PRIVATE ps2_runtime)
    if(TARGET ben10_vu1_native)
        target_link_libraries(ben10_poe PRIVATE ben10_vu1_native)
    endif()

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

    # The sampling profiler includes <windows.h>, which clashes with raylib
    # and the runtime's own Win32 declarations: compiled on its own.
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/diagnostics/sample_profiler.cpp" PROPERTIES
        SKIP_UNITY_BUILD_INCLUSION ON
        SKIP_PRECOMPILE_HEADERS ON)

    if(MSVC)
        # Large generated functions exceed the COFF format's section limit.
        target_compile_options(ben10_poe PRIVATE /bigobj /FS)
        # A linker map (function name -> address) next to the executable, for
        # tools/perf/win_profile.py (the Release build has no debug symbols).
        target_link_options(ben10_poe PRIVATE "/MAP:$<TARGET_FILE_DIR:ben10_poe>/ben10_poe.map")
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
