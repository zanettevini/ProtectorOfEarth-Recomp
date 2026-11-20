# CMakeLists.txt: build do Protector of Earth Recomp.
#
# O que é montado:
#   1. O PS2Recomp (thirdparty/PS2Recomp, submódulo fixado): o recompilador
#      ps2_recomp e a biblioteca de runtime ps2_runtime.
#   2. O alvo ben10_codegen: roda o ps2_recomp com ben10_poe.toml e gera o C++
#      do jogo em generated/default/ (nunca versionado).
#   3. O executável ben10_poe: o C++ gerado + o runtime + o código próprio do
#      port em src/. Só existe quando generated/default/ já tem arquivos:
#      depois do primeiro codegen, rode o configure de novo.
#   4. Os testes (tests/, GoogleTest): não precisam do jogo.
#
# Passo a passo completo: docs/01-building.md.

cmake_minimum_required(VERSION 3.21)
project(ProtectorOfEarthRecomp VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BEN10_BUILD_GAME  "Monta o executável do jogo (precisa de generated/default)" ON)
option(BEN10_BUILD_TESTS "Monta os testes automatizados" ON)

# ---------------------------------------------------------------------------
# CPU mínima
# ---------------------------------------------------------------------------
# O código gerado e o runtime usam intrínsecos SSE4.1 (_mm_blendv_ps,
# _mm_min_epi32...) para os registradores de 128 bits do EE. O MSVC aceita
# intrínsecos sem flag; GCC e Clang precisam saber que a CPU tem SSE4.
# x86-64-v2 = SSE4.2 + POPCNT: qualquer CPU x86-64 desde ~2009. É a mesma
# escolha do Gitaroo Man Native. Sem isto, o primeiro build parou com
# "inlining failed ... target specific option mismatch" (findings/14 s.3).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(BEN10_ARCH_FLAGS "-march=x86-64-v2" CACHE STRING "Flags de CPU para GCC/Clang")
    add_compile_options(${BEN10_ARCH_FLAGS})
endif()

# ---------------------------------------------------------------------------
# PS2Recomp
# ---------------------------------------------------------------------------
# Só o recompilador e o runtime. As opções abaixo valem para o submódulo;
# os valores escolhidos aqui ficam documentados em docs/01-building.md.
set(PS2X_BUILD_RECOMP   ON  CACHE BOOL "" FORCE)
set(PS2X_BUILD_RUNTIME  ON  CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST     OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO   OFF CACHE BOOL "" FORCE)
# Logs por função (PS2_FUNCTION_LOG_TRACKER) põem uma chamada de log em cada
# uma das ~20 mil funções geradas: build e execução muito mais lentos.
# Desligado por padrão; ligue para depurar.
set(PS2X_ENABLE_AGRESSIVE_LOGS OFF CACHE BOOL "")
# Painel ImGui de depuração do PS2Recomp: desligado até ser útil.
set(PS2X_ENABLE_DEBUG_UI OFF CACHE BOOL "")
# Decodificação de vídeo MPEG pelo FFmpeg: o jogo liga a libipu
# (findings/09 s.7), mas ainda não se sabe se tem vídeos. Desligado até
# precisar (não exige o FFmpeg instalado).
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "")

# Nossas correções do PS2Recomp (patches/ps2recomp/, cada uma com cabeçalho:
# o que quebrou, por quê, a correção, como foi achado). Aplicadas aqui, em
# ordem, só se ainda não estiverem: o submódulo continua no commit fixado e
# ninguém precisa aplicar nada à mão. Para desfazer: git -C
# thirdparty/PS2Recomp checkout . (volta ao original).
find_package(Git REQUIRED)
file(GLOB BEN10_PS2RECOMP_PATCHES "${CMAKE_SOURCE_DIR}/patches/ps2recomp/*.patch")
list(SORT BEN10_PS2RECOMP_PATCHES)
foreach(BEN10_PATCH IN LISTS BEN10_PS2RECOMP_PATCHES)
    get_filename_component(BEN10_PATCH_NAME "${BEN10_PATCH}" NAME)
    # Se o patch "desaplica" limpo, ele já está aplicado.
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
            message(FATAL_ERROR "Patch ${BEN10_PATCH_NAME} nao aplicou: ${BEN10_PATCH_ERROR}\n"
                "O submodulo esta no commit certo? (git submodule update --init)")
        endif()
        message(STATUS "PS2Recomp: aplicado ${BEN10_PATCH_NAME}")
    endif()
endforeach()

add_subdirectory(thirdparty/PS2Recomp)

# O PS2Recomp também define o executável genérico ps2EntryRunner (sem jogo).
# Não usamos: fica fora do "build all".
if(TARGET ps2EntryRunner)
    set_target_properties(ps2EntryRunner PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# ---------------------------------------------------------------------------
# Codegen: ELF do jogo -> C++
# ---------------------------------------------------------------------------
add_custom_target(ben10_codegen
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/generated/default"
    COMMAND $<TARGET_FILE:ps2_recomp> "${CMAKE_SOURCE_DIR}/ben10_poe.toml"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS ps2_recomp
    COMMENT "Recompilando game/SLUS_216.61 para generated/default/"
    VERBATIM)

# ---------------------------------------------------------------------------
# O jogo
# ---------------------------------------------------------------------------
file(GLOB BEN10_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/generated/default/*.cpp")

if(BEN10_BUILD_GAME AND BEN10_GENERATED_SOURCES)
    list(LENGTH BEN10_GENERATED_SOURCES BEN10_GENERATED_COUNT)
    message(STATUS "ben10_poe: ${BEN10_GENERATED_COUNT} arquivos gerados")

    # O main() é o do runner do PS2Recomp: carrega o ELF, prepara o runtime e
    # chama o ponto de entrada recompilado. O port terá o próprio em src/
    # quando precisar (janela, opções, saves).
    file(GLOB BEN10_PORT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

    add_executable(ben10_poe
        ${BEN10_GENERATED_SOURCES}
        ${BEN10_PORT_SOURCES}
        "${CMAKE_SOURCE_DIR}/thirdparty/PS2Recomp/ps2xRuntime/src/main.cpp")

    target_include_directories(ben10_poe PRIVATE
        "${CMAKE_SOURCE_DIR}/generated/default"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(ben10_poe PRIVATE ps2_runtime)

    # Igual ao ps2EntryRunner: junta ~32 arquivos gerados por unidade de
    # compilação e pré-compila os cabeçalhos pesados. Sem isso, cada um dos
    # ~20 mil arquivos reprocessa os mesmos cabeçalhos.
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
        # Funções geradas grandes passam do limite de seções do formato COFF.
        target_compile_options(ben10_poe PRIVATE /bigobj /FS)
    endif()
elseif(BEN10_BUILD_GAME)
    message(STATUS "ben10_poe: generated/default/ vazio. Rode o alvo ben10_codegen e configure de novo.")
endif()

# ---------------------------------------------------------------------------
# Testes
# ---------------------------------------------------------------------------
if(BEN10_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
