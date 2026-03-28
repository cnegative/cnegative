if(NOT DEFINED ROOT_DIR)
    message(FATAL_ERROR "ROOT_DIR is required")
endif()

if(NOT DEFINED CNEGC_BIN)
    message(FATAL_ERROR "CNEGC_BIN is required")
endif()

if(NOT DEFINED EXE_SUFFIX)
    set(EXE_SUFFIX "")
endif()

find_program(LLVM_AS_BIN NAMES llvm-as-18 llvm-as)
if(NOT LLVM_AS_BIN)
    message(FATAL_ERROR "expected llvm-as-18 or llvm-as in PATH for backend smoke tests")
endif()

set(TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/cnegc-smoke")
file(REMOVE_RECURSE "${TMP_DIR}")
file(MAKE_DIRECTORY "${TMP_DIR}")

set(TMP_VALID "${TMP_DIR}/valid.txt")
set(TMP_INVALID "${TMP_DIR}/invalid.txt")
set(TMP_IR "${TMP_DIR}/ir.txt")
set(TMP_LL "${TMP_DIR}/module.ll")
set(TMP_BC "${TMP_DIR}/module.bc")
set(TMP_OBJ "${TMP_DIR}/module.obj")
set(TMP_BIN "${TMP_DIR}/program${EXE_SUFFIX}")
set(TMP_RUN "${TMP_DIR}/run.txt")
set(TMP_STDIN "${TMP_DIR}/stdin.txt")

function(cn_run_expect_success output_path)
    set(command_args ${ARGN})

    execute_process(
        COMMAND ${command_args}
        WORKING_DIRECTORY "${ROOT_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )

    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "command failed with exit ${result}:\n${command_args}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}"
        )
    endif()

    file(WRITE "${output_path}" "${stdout_text}${stderr_text}")
endfunction()

function(cn_run_expect_failure output_path)
    set(command_args ${ARGN})

    execute_process(
        COMMAND ${command_args}
        WORKING_DIRECTORY "${ROOT_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )

    if(result EQUAL 0)
        message(FATAL_ERROR "expected command to fail:\n${command_args}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}")
    endif()

    file(WRITE "${output_path}" "${stdout_text}${stderr_text}")
endfunction()

function(cn_run_binary output_path expected_status input_file)
    set(command_args ${ARGN})

    if(NOT input_file STREQUAL "")
        execute_process(
            COMMAND ${command_args}
            WORKING_DIRECTORY "${ROOT_DIR}"
            INPUT_FILE "${input_file}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE stdout_text
            ERROR_VARIABLE stderr_text
        )
    else()
        execute_process(
            COMMAND ${command_args}
            WORKING_DIRECTORY "${ROOT_DIR}"
            RESULT_VARIABLE result
            OUTPUT_VARIABLE stdout_text
            ERROR_VARIABLE stderr_text
        )
    endif()

    if(NOT result EQUAL expected_status)
        message(FATAL_ERROR
            "expected exit ${expected_status}, got ${result} for command:\n${command_args}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}"
        )
    endif()

    file(WRITE "${output_path}" "${stdout_text}${stderr_text}")
endfunction()

function(cn_assert_contains file_path expected_text)
    file(READ "${file_path}" content)
    string(FIND "${content}" "${expected_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "expected '${expected_text}' in ${file_path}\nactual contents:\n${content}")
    endif()
endfunction()

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_basic.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_structs_arrays.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_modules_ptr_result.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_imported_structs.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_llvm_backend.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_strings.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_input_equality.cneg)

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" bench-lexer examples/valid_basic.cneg 5)
cn_assert_contains("${TMP_VALID}" "tokens_per_second:")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_imported_structs.cneg)
cn_assert_contains("${TMP_IR}" "fn valid_imported_structs.main() -> int")
cn_assert_contains("${TMP_IR}" "return w.point.y;")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_modules_ptr_result.cneg)
cn_assert_contains("${TMP_IR}" "return ok (a / b);")
cn_assert_contains("${TMP_IR}" "deref px")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_basic.cneg)
cn_assert_contains("${TMP_LL}" "@cn_valid_basic__main")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_structs_arrays.cneg)
cn_assert_contains("${TMP_LL}" "%cn_valid_structs_arrays__User = type")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_modules_ptr_result.cneg)
cn_assert_contains("${TMP_LL}" "@malloc")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_imported_structs.cneg)
cn_assert_contains("${TMP_LL}" "%cn_shapes__Point = type")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_llvm_backend.cneg)
cn_assert_contains("${TMP_LL}" "@cn_math__add")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_strings.cneg)
cn_assert_contains("${TMP_LL}" "@.cn.str.")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_input_equality.cneg)
cn_assert_contains("${TMP_LL}" "@cn_input")
cn_assert_contains("${TMP_LL}" "@cn_free_str")
cn_assert_contains("${TMP_LL}" "@strcmp")
cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${TMP_LL}" -o "${TMP_BC}")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_if_int_condition.cneg)
cn_assert_contains("${TMP_INVALID}" "E3005")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_struct_missing_field.cneg)
cn_assert_contains("${TMP_INVALID}" "missing field")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_missing_semicolon.cneg)
cn_assert_contains("${TMP_INVALID}" "expected ';'")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_unimported_type.cneg)
cn_assert_contains("${TMP_INVALID}" "E3012")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_private_import_type.cneg)
cn_assert_contains("${TMP_INVALID}" "E3012")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_public_private_type.cneg)
cn_assert_contains("${TMP_INVALID}" "E3023")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_result_value_guard.cneg)
cn_assert_contains("${TMP_INVALID}" "E3024")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_implicit_return.cneg)
cn_assert_contains("${TMP_INVALID}" "E3007")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" obj examples/valid_basic.cneg "${TMP_OBJ}")
if(NOT EXISTS "${TMP_OBJ}")
    message(FATAL_ERROR "expected object output for valid_basic.cneg")
endif()
file(SIZE "${TMP_OBJ}" obj_size)
if(obj_size EQUAL 0)
    message(FATAL_ERROR "expected non-empty object output for valid_basic.cneg")
endif()

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_structs_arrays.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 200 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_modules_ptr_result.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 5 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_llvm_backend.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 3 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "3")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_strings.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "hello from cnegative")

file(WRITE "${TMP_STDIN}" "neo\n")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_input_equality.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 1 "${TMP_STDIN}" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "neo")
