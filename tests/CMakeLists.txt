# tests/CMakeLists.txt: testes automatizados (GoogleTest). Não precisam do jogo.
#
# ben10_instruction_tests: testes por instrução de ponta a ponta.
#   make_test_elf.py escreve um ELF de teste -> o ps2_recomp traduz ->
#   o C++ gerado é compilado aqui com tests/instructions/*.cpp.
#   Ver o cabeçalho de tests/instructions/test_ee_instructions.cpp.

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE)
# No Windows, usar o mesmo runtime de C do resto do projeto.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BEN10_TEST_CASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/instruction_cases")
set(BEN10_TEST_GENERATED
    "${BEN10_TEST_CASE_DIR}/generated/ps2_recompiled_functions.cpp"
    "${BEN10_TEST_CASE_DIR}/generated/register_functions.cpp")

add_custom_command(
    OUTPUT ${BEN10_TEST_GENERATED}
           "${BEN10_TEST_CASE_DIR}/test_cases.h"
           "${BEN10_TEST_CASE_DIR}/generated/ps2_recompiled_functions.h"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${BEN10_TEST_CASE_DIR}/generated"
    # -I: o Python não carrega módulos da pasta do script nem da atual.
    COMMAND Python3::Interpreter -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_test_elf.py" "${BEN10_TEST_CASE_DIR}"
    COMMAND $<TARGET_FILE:ps2_recomp> "${BEN10_TEST_CASE_DIR}/test_program.toml"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_test_elf.py" ps2_recomp
    COMMENT "Gerando e recompilando o ELF de teste"
    VERBATIM)

file(GLOB BEN10_INSTRUCTION_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/instructions/*.cpp")

add_executable(ben10_instruction_tests
    ${BEN10_INSTRUCTION_TEST_SOURCES}
    ${BEN10_TEST_GENERATED})
target_include_directories(ben10_instruction_tests PRIVATE
    "${BEN10_TEST_CASE_DIR}"
    "${BEN10_TEST_CASE_DIR}/generated")
target_link_libraries(ben10_instruction_tests PRIVATE ps2_runtime GTest::gtest_main)
if(MSVC)
    target_compile_options(ben10_instruction_tests PRIVATE /bigobj)
endif()

include(GoogleTest)
gtest_discover_tests(ben10_instruction_tests DISCOVERY_TIMEOUT 60)
