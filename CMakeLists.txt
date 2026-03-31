cmake_minimum_required(VERSION 3.20)

project(cnegative LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CNEGATIVE_ENABLE_ASM "Enable x86_64 assembly hot paths where supported" ON)
set(CNEGATIVE_C_SOURCES
    src/backend/llvm_runtime.c
    src/backend/stdlib/core.c
    src/backend/stdlib/env.c
    src/backend/stdlib/fs.c
    src/backend/stdlib/io.c
    src/backend/stdlib/math.c
    src/backend/stdlib/net.c
    src/backend/stdlib/parse.c
    src/backend/stdlib/path.c
    src/backend/stdlib/process.c
    src/backend/stdlib/strings.c
    src/backend/stdlib/term.c
    src/backend/stdlib/time.c
    src/backend/stdlib/x11.c
    src/asm/scan_fallback.c
    src/backend/llvm_text.c
    src/backend/toolchain.c
    src/cli/main.c
    src/ir/ir.c
    src/ir/lower.c
    src/ir/optimize.c
    src/lex/lexer.c
    src/lex/token.c
    src/parse/ast.c
    src/parse/parser.c
    src/sema/typecheck.c
    src/support/diagnostics.c
    src/support/memory.c
    src/support/project.c
    src/support/source.c
)


set(CNEGATIVE_ASM_SOURCES)
if(CNEGATIVE_ENABLE_ASM)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND
       (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
        enable_language(ASM)
        list(APPEND CNEGATIVE_ASM_SOURCES src/asm/x86_64_scan.S)
    endif()
endif()

add_executable(cnegc ${CNEGATIVE_C_SOURCES} ${CNEGATIVE_ASM_SOURCES})
target_include_directories(cnegc PRIVATE include)

if(CNEGATIVE_ASM_SOURCES)
    target_compile_definitions(cnegc PRIVATE CN_USE_X86_64_ASM_SCAN=1)
endif()

if(MSVC)
    target_compile_options(cnegc PRIVATE /W4 /WX)
else()
    target_compile_options(cnegc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

set_target_properties(cnegc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build"
)

add_custom_target(check-lines
    COMMAND ${CMAKE_COMMAND}
        -DROOT_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckFileLines.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking file line limits"
)

add_custom_target(smoke
    COMMAND ${CMAKE_COMMAND}
        -DROOT_DIR=${CMAKE_SOURCE_DIR}
        -DCNEGC_BIN=$<TARGET_FILE:cnegc>
        -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
        -P ${CMAKE_SOURCE_DIR}/cmake/SmokeTests.cmake
    DEPENDS cnegc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running smoke tests"
)

include(CTest)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND)
    add_custom_target(net-test
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_tcp.cneg ${CMAKE_SOURCE_DIR}/build/net-main-python
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_tcp_server.cneg ${CMAKE_SOURCE_DIR}/build/net-server-python
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_tcp_client.cneg ${CMAKE_SOURCE_DIR}/build/net-client-python
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/net_integration.py
        DEPENDS cnegc
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running optional Python network integration test"
    )

    add_custom_target(udp-test
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_udp.cneg ${CMAKE_SOURCE_DIR}/build/udp-main-python
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_udp_server.cneg ${CMAKE_SOURCE_DIR}/build/udp-server-python
        COMMAND $<TARGET_FILE:cnegc> build ${CMAKE_SOURCE_DIR}/examples/valid_stdlib_net_udp_client.cneg ${CMAKE_SOURCE_DIR}/build/udp-client-python
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/net_integration.py
            --main-binary ${CMAKE_SOURCE_DIR}/build/udp-main-python
            --server-binary ${CMAKE_SOURCE_DIR}/build/udp-server-python
            --client-binary ${CMAKE_SOURCE_DIR}/build/udp-client-python
        DEPENDS cnegc
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running optional Python UDP integration test"
    )
endif()

if(BUILD_TESTING)
    add_test(
        NAME cnegc-check-lines
        COMMAND ${CMAKE_COMMAND}
            -DROOT_DIR=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/CheckFileLines.cmake
    )

    add_test(
        NAME cnegc-smoke
        COMMAND ${CMAKE_COMMAND}
            -DROOT_DIR=${CMAKE_SOURCE_DIR}
            -DCNEGC_BIN=$<TARGET_FILE:cnegc>
            -DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
            -P ${CMAKE_SOURCE_DIR}/cmake/SmokeTests.cmake
    )

endif()
