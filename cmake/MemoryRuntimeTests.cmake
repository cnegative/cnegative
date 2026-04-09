if(NOT DEFINED ROOT_DIR)
    message(FATAL_ERROR "ROOT_DIR is required")
endif()

if(NOT DEFINED MEMORY_RUNTIME_BIN)
    message(FATAL_ERROR "MEMORY_RUNTIME_BIN is required")
endif()

function(cn_memory_expect_success case_name)
    execute_process(
        COMMAND "${MEMORY_RUNTIME_BIN}" "${case_name}"
        WORKING_DIRECTORY "${ROOT_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )

    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "expected success for ${case_name}\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}"
        )
    endif()
endfunction()

function(cn_memory_expect_failure case_name expected_code)
    execute_process(
        COMMAND "${MEMORY_RUNTIME_BIN}" "${case_name}"
        WORKING_DIRECTORY "${ROOT_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )

    if(result EQUAL 0)
        message(FATAL_ERROR "expected ${case_name} to fail")
    endif()

    string(FIND "${stderr_text}" "${expected_code}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR
            "expected ${expected_code} in ${case_name} stderr\nstdout:\n${stdout_text}\nstderr:\n${stderr_text}"
        )
    endif()
endfunction()

cn_memory_expect_success("ok")
cn_memory_expect_failure("double-free" "R4010")
cn_memory_expect_failure("interior-free" "R4011")
cn_memory_expect_failure("overflow" "R4016")
cn_memory_expect_failure("underflow" "R4017")
cn_memory_expect_failure("use-after-free" "R4013")
