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
find_program(CLANG_IR_BIN NAMES clang-18 clang)
if(NOT LLVM_AS_BIN AND NOT CLANG_IR_BIN)
    message(FATAL_ERROR "expected llvm-as-18, llvm-as, clang-18, or clang in PATH for backend smoke tests")
endif()

set(TMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/cnegc-smoke")
file(REMOVE_RECURSE "${TMP_DIR}")
file(MAKE_DIRECTORY "${TMP_DIR}")
file(MAKE_DIRECTORY "${ROOT_DIR}/build")

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

    file(WRITE "${output_path}" "${stdout_text}")
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

    file(WRITE "${output_path}" "${stdout_text}")
endfunction()

function(cn_assert_contains file_path expected_text)
    file(READ "${file_path}" content)
    string(FIND "${content}" "${expected_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "expected '${expected_text}' in ${file_path}\nactual contents:\n${content}")
    endif()
endfunction()

function(cn_verify_llvm_ir input_path output_path)
    if(LLVM_AS_BIN)
        cn_run_expect_success("${TMP_VALID}" "${LLVM_AS_BIN}" "${input_path}" -o "${output_path}")
    else()
        cn_run_expect_success("${TMP_VALID}" "${CLANG_IR_BIN}" -c -x ir "${input_path}" -o "${output_path}")
    endif()
endfunction()

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_basic.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_structs_arrays.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_modules_ptr_result.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_imported_structs.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_llvm_backend.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_strings.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_input_equality.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_consts_strings.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_more.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_io_fs.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_io_read.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_time_dirs.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_net_fs.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_net_tcp.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_net_udp.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_math_process.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_path_fs_extra.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_term.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_term_more.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_term_render.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_u8.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_slice.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_bytes_text.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_ipc.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_stdlib_lines.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_if_expr.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_defer.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_try.cneg)
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" check examples/valid_raw_strings.cneg)

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" bench-lexer examples/valid_basic.cneg 5)
cn_assert_contains("${TMP_VALID}" "tokens_per_second:")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_imported_structs.cneg)
cn_assert_contains("${TMP_IR}" "fn valid_imported_structs.main() -> int")
cn_assert_contains("${TMP_IR}" "return w.point.y;")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_modules_ptr_result.cneg)
cn_assert_contains("${TMP_IR}" "return ok (a / b);")
cn_assert_contains("${TMP_IR}" "deref px")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_consts_strings.cneg)
cn_assert_contains("${TMP_IR}" "const valid_consts_strings.LOCAL:int = 20;")
cn_assert_contains("${TMP_IR}" "if true {")
cn_assert_contains("${TMP_IR}" "return 20;")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib.cneg)
cn_assert_contains("${TMP_IR}" "std.strings.concat")
cn_assert_contains("${TMP_IR}" "std.fs.read_text")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_more.cneg)
cn_assert_contains("${TMP_IR}" "std.env.get")
cn_assert_contains("${TMP_IR}" "std.path.join")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_io_fs.cneg)
cn_assert_contains("${TMP_IR}" "std.io.write_line")
cn_assert_contains("${TMP_IR}" "std.fs.append_text")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_time_dirs.cneg)
cn_assert_contains("${TMP_IR}" "std.time.now_ms")
cn_assert_contains("${TMP_IR}" "std.fs.create_dir")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_net_fs.cneg)
cn_assert_contains("${TMP_IR}" "std.net.join_host_port")
cn_assert_contains("${TMP_IR}" "std.fs.file_size")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_net_tcp.cneg)
cn_assert_contains("${TMP_IR}" "std.net.tcp_connect")
cn_assert_contains("${TMP_IR}" "std.net.tcp_listen")
cn_assert_contains("${TMP_IR}" "std.net.recv")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_net_udp.cneg)
cn_assert_contains("${TMP_IR}" "std.net.udp_bind")
cn_assert_contains("${TMP_IR}" "std.net.udp_send_to")
cn_assert_contains("${TMP_IR}" "std.net.udp_recv_from")
cn_assert_contains("${TMP_IR}" "result std.net.UdpPacket")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_math_process.cneg)
cn_assert_contains("${TMP_IR}" "%")
cn_assert_contains("${TMP_IR}" "std.math.clamp")
cn_assert_contains("${TMP_IR}" "std.math.gcd")
cn_assert_contains("${TMP_IR}" "std.math.between")
cn_assert_contains("${TMP_IR}" "std.process.platform")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_path_fs_extra.cneg)
cn_assert_contains("${TMP_IR}" "std.fs.copy")
cn_assert_contains("${TMP_IR}" "std.path.extension")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_term.cneg)
cn_assert_contains("${TMP_IR}" "std.term.columns")
cn_assert_contains("${TMP_IR}" "std.term.enter_raw_mode")
cn_assert_contains("${TMP_IR}" "std.term.read_byte")
cn_assert_contains("${TMP_IR}" "std.term.read_event")
cn_assert_contains("${TMP_IR}" "std.term.read_paste")
cn_assert_contains("${TMP_IR}" "std.term.enable_mouse")
cn_assert_contains("${TMP_IR}" "std.term.enable_bracketed_paste")
cn_assert_contains("${TMP_IR}" "std.term.enter_alt_screen")
cn_assert_contains("${TMP_IR}" "result std.term.Event")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_term_more.cneg)
cn_assert_contains("${TMP_IR}" "std.term.term_name")
cn_assert_contains("${TMP_IR}" "std.term.supports_truecolor")
cn_assert_contains("${TMP_IR}" "std.term.read_event_timeout")
cn_assert_contains("${TMP_IR}" "std.term.rgb")
cn_assert_contains("${TMP_IR}" "std.term.string_width")
cn_assert_contains("${TMP_IR}" "std.term.set_scroll_region")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_term_render.cneg)
cn_assert_contains("${TMP_IR}" "std.term.buffer_new")
cn_assert_contains("${TMP_IR}" "std.term.buffer_resize")
cn_assert_contains("${TMP_IR}" "std.term.render_diff_clip")
cn_assert_contains("${TMP_IR}" "result ptr std.term.Buffer")
cn_assert_contains("${TMP_IR}" "term.Clip")
cn_assert_contains("${TMP_IR}" "std.term.codepoint_width")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_u8.cneg)
cn_assert_contains("${TMP_IR}" "fn valid_u8.main() -> u8")
cn_assert_contains("${TMP_IR}" "return if (value == 0) {")
cn_assert_contains("${TMP_IR}" "packet.kind == 42")
cn_assert_contains("${TMP_IR}" "packet.payload[1] == 1")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_slice.cneg)
cn_assert_contains("${TMP_IR}" "fn valid_slice.sum_head(xs:slice int) -> int")
cn_assert_contains("${TMP_IR}" "let view:slice int = slice data;")
cn_assert_contains("${TMP_IR}" "let middle:slice int = view[1..3];")
cn_assert_contains("${TMP_IR}" "let prefix:slice int = data[0..2];")
cn_assert_contains("${TMP_IR}" "third(data[1..4])")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_bytes_text.cneg)
cn_assert_contains("${TMP_IR}" "std.bytes.append")
cn_assert_contains("${TMP_IR}" "let view:slice u8 = std.bytes.view(buffer);")
cn_assert_contains("${TMP_IR}" "std.text.build")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_lines.cneg)
cn_assert_contains("${TMP_IR}" "std.lines.insert")
cn_assert_contains("${TMP_IR}" "std.lines.get")
cn_assert_contains("${TMP_IR}" "result ptr std.lines.Buffer")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_stdlib_ipc.cneg)
cn_assert_contains("${TMP_IR}" "std.ipc.spawn")
cn_assert_contains("${TMP_IR}" "std.ipc.request_line")
cn_assert_contains("${TMP_IR}" "std.ipc.stderr_read_line")
cn_assert_contains("${TMP_IR}" "result ptr std.ipc.Child")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_if_expr.cneg)
cn_assert_contains("${TMP_IR}" "let picked:int = if flag {")
cn_assert_contains("${TMP_IR}" "return if (value == 0) {")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_defer.cneg)
cn_assert_contains("${TMP_IR}" "print(\"cleanup\");")
cn_assert_contains("${TMP_IR}" "if (value < 0) {")

cn_run_expect_success("${TMP_IR}" "${CNEGC_BIN}" ir examples/valid_try.cneg)
cn_assert_contains("${TMP_IR}" "let __cn_try_")
cn_assert_contains("${TMP_IR}" "if !__cn_try_")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_basic.cneg)
cn_assert_contains("${TMP_LL}" "@cn_valid_basic__main")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_structs_arrays.cneg)
cn_assert_contains("${TMP_LL}" "%cn_valid_structs_arrays__User = type")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_modules_ptr_result.cneg)
cn_assert_contains("${TMP_LL}" "@malloc")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_imported_structs.cneg)
cn_assert_contains("${TMP_LL}" "%cn_shapes__Point = type")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_llvm_backend.cneg)
cn_assert_contains("${TMP_LL}" "@cn_math__add")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_strings.cneg)
cn_assert_contains("${TMP_LL}" "@.cn.str.")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_input_equality.cneg)
cn_assert_contains("${TMP_LL}" "@cn_input")
cn_assert_contains("${TMP_LL}" "@cn_free_str")
cn_assert_contains("${TMP_LL}" "@strcmp")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_consts_strings.cneg)
cn_assert_contains("${TMP_LL}" "@cn_concat_str")
cn_assert_contains("${TMP_LL}" "call ptr @cn_dup_cstr")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib.cneg)
cn_assert_contains("${TMP_LL}" "@cn_parse_int")
cn_assert_contains("${TMP_LL}" "@cn_fs_read_text")
cn_assert_contains("${TMP_LL}" "@cn_starts_with")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_more.cneg)
cn_assert_contains("${TMP_LL}" "@cn_env_get")
cn_assert_contains("${TMP_LL}" "@cn_path_join")
cn_assert_contains("${TMP_LL}" "@cn_fs_remove")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_io_fs.cneg)
cn_assert_contains("${TMP_LL}" "@cn_write_str")
cn_assert_contains("${TMP_LL}" "@cn_fs_append_text")
cn_assert_contains("${TMP_LL}" "@cn_fs_rename")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_time_dirs.cneg)
cn_assert_contains("${TMP_LL}" "@cn_time_now_ms")
cn_assert_contains("${TMP_LL}" "@cn_time_sleep_ms")
cn_assert_contains("${TMP_LL}" "@cn_fs_create_dir")
cn_assert_contains("${TMP_LL}" "@cn_fs_remove_dir")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_net_fs.cneg)
cn_assert_contains("${TMP_LL}" "@cn_net_is_ipv4")
cn_assert_contains("${TMP_LL}" "@cn_net_join_host_port")
cn_assert_contains("${TMP_LL}" "@cn_fs_file_size")
cn_assert_contains("${TMP_LL}" "@cn_fs_cwd")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_net_tcp.cneg)
cn_assert_contains("${TMP_LL}" "@cn_net_tcp_connect")
cn_assert_contains("${TMP_LL}" "@cn_net_tcp_listen")
cn_assert_contains("${TMP_LL}" "@cn_net_send")
cn_assert_contains("${TMP_LL}" "@cn_net_recv")
cn_assert_contains("${TMP_LL}" "@cn_net_accept")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_net_udp.cneg)
cn_assert_contains("${TMP_LL}" "%cn_std_x2E_net__UdpPacket = type")
cn_assert_contains("${TMP_LL}" "@cn_net_udp_bind")
cn_assert_contains("${TMP_LL}" "@cn_net_udp_send_to")
cn_assert_contains("${TMP_LL}" "@cn_net_udp_recv_from")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_math_process.cneg)
cn_assert_contains("${TMP_LL}" "@cn_math_clamp")
cn_assert_contains("${TMP_LL}" "@cn_math_gcd")
cn_assert_contains("${TMP_LL}" "@cn_math_between")
cn_assert_contains("${TMP_LL}" "srem ")
cn_assert_contains("${TMP_LL}" "@cn_process_platform")
cn_assert_contains("${TMP_LL}" "@cn_process_exit")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_path_fs_extra.cneg)
cn_assert_contains("${TMP_LL}" "@cn_fs_copy")
cn_assert_contains("${TMP_LL}" "@cn_path_extension")
cn_assert_contains("${TMP_LL}" "@cn_path_stem")
cn_assert_contains("${TMP_LL}" "@cn_path_is_absolute")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_term.cneg)
cn_assert_contains("${TMP_LL}" "@cn_term_columns")
cn_assert_contains("${TMP_LL}" "@cn_term_enter_raw_mode")
cn_assert_contains("${TMP_LL}" "@cn_term_read_byte")
cn_assert_contains("${TMP_LL}" "@cn_term_read_event")
cn_assert_contains("${TMP_LL}" "@cn_term_read_paste")
cn_assert_contains("${TMP_LL}" "@cn_term_enable_mouse")
cn_assert_contains("${TMP_LL}" "@cn_term_enable_bracketed_paste")
cn_assert_contains("${TMP_LL}" "@cn_term_move_cursor")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_term_more.cneg)
cn_assert_contains("${TMP_LL}" "@cn_term_term_name")
cn_assert_contains("${TMP_LL}" "@cn_term_supports_truecolor")
cn_assert_contains("${TMP_LL}" "@cn_term_read_event_timeout")
cn_assert_contains("${TMP_LL}" "@cn_term_rgb")
cn_assert_contains("${TMP_LL}" "@cn_term_set_scroll_region")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_term_render.cneg)
cn_assert_contains("${TMP_LL}" "@cn_term_set_style")
cn_assert_contains("${TMP_LL}" "@cn_term_buffer_new")
cn_assert_contains("${TMP_LL}" "@cn_term_buffer_resize")
cn_assert_contains("${TMP_LL}" "@cn_term_render_diff_clip")
cn_assert_contains("${TMP_LL}" "%cn_std_x2E_term__Clip = type")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_u8.cneg)
cn_assert_contains("${TMP_LL}" "alloca i8")
cn_assert_contains("${TMP_LL}" "icmp eq i8")
cn_assert_contains("${TMP_LL}" "zext i8")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_slice.cneg)
cn_assert_contains("${TMP_LL}" "{ ptr, i64 }")
cn_assert_contains("${TMP_LL}" "extractvalue { ptr, i64 }")
cn_assert_contains("${TMP_LL}" "sub i64")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_bytes_text.cneg)
cn_assert_contains("${TMP_LL}" "@cn_bytes_append")
cn_assert_contains("${TMP_LL}" "@cn_text_build")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_lines.cneg)
cn_assert_contains("${TMP_LL}" "@cn_lines_insert")
cn_assert_contains("${TMP_LL}" "@cn_lines_get")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_stdlib_ipc.cneg)
cn_assert_contains("${TMP_LL}" "@cn_ipc_spawn")
cn_assert_contains("${TMP_LL}" "@cn_ipc_native_spawn")
cn_assert_contains("${TMP_LL}" "@cn_ipc_request_line")
cn_assert_contains("${TMP_LL}" "@cn_ipc_stdout_read_line")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_success("${TMP_LL}" "${CNEGC_BIN}" llvm-ir examples/valid_if_expr.cneg)
cn_assert_contains("${TMP_LL}" "br i1")
cn_assert_contains("${TMP_LL}" "alloca i64")
cn_verify_llvm_ir("${TMP_LL}" "${TMP_BC}")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_if_int_condition.cneg)
cn_assert_contains("${TMP_INVALID}" "E3005")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_if_expr_int_condition.cneg)
cn_assert_contains("${TMP_INVALID}" "E3005")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_if_expr_void.cneg)
cn_assert_contains("${TMP_INVALID}" "E3029")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_if_expr_branch_type.cneg)
cn_assert_contains("${TMP_INVALID}" "E3030")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_addr_target.cneg)
cn_assert_contains("${TMP_INVALID}" "E3031")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_deref_non_ptr.cneg)
cn_assert_contains("${TMP_INVALID}" "E3032")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_assignment_target.cneg)
cn_assert_contains("${TMP_INVALID}" "E1006")

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

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_const_runtime.cneg)
cn_assert_contains("${TMP_INVALID}" "E3025")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_const_cycle.cneg)
cn_assert_contains("${TMP_INVALID}" "E3026")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_parse_recovery.cneg)
file(READ "${TMP_INVALID}" invalid_recovery_output)
string(REGEX MATCHALL "expected ';'" recovery_matches "${invalid_recovery_output}")
list(LENGTH recovery_matches recovery_count)
if(recovery_count LESS 4)
    message(FATAL_ERROR "expected parser recovery to report multiple semicolon errors\nactual output:\n${invalid_recovery_output}")
endif()

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_u8_range.cneg)
cn_assert_contains("${TMP_INVALID}" "u8 literal out of range")
cn_assert_contains("${TMP_INVALID}" "E3028")

cn_run_expect_failure("${TMP_INVALID}" "${CNEGC_BIN}" check examples/invalid_try_non_result.cneg)
cn_assert_contains("${TMP_INVALID}" "E3033")
cn_assert_contains("${TMP_INVALID}" "E3034")

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

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_consts_strings.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 20 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "hello world")

file(REMOVE "${ROOT_DIR}/build/std_demo.txt")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
if(NOT EXISTS "${ROOT_DIR}/build/std_demo.txt")
    message(FATAL_ERROR "expected valid_stdlib binary to create build/std_demo.txt")
endif()
file(READ "${ROOT_DIR}/build/std_demo.txt" std_demo_text)
if(NOT std_demo_text STREQUAL "42")
    message(FATAL_ERROR "expected valid_stdlib binary to write 42 into build/std_demo.txt\nactual contents:\n${std_demo_text}")
endif()

file(REMOVE "${ROOT_DIR}/build/stdlib_more.txt")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_more.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
if(EXISTS "${ROOT_DIR}/build/stdlib_more.txt")
    message(FATAL_ERROR "expected valid_stdlib_more binary to remove build/stdlib_more.txt")
endif()

file(REMOVE "${ROOT_DIR}/build/io_fs_demo.txt")
file(REMOVE "${ROOT_DIR}/build/io_fs_demo_renamed.txt")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_io_fs.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "io:ready")
if(EXISTS "${ROOT_DIR}/build/io_fs_demo.txt" OR EXISTS "${ROOT_DIR}/build/io_fs_demo_renamed.txt")
    message(FATAL_ERROR "expected valid_stdlib_io_fs binary to clean up io demo files")
endif()

file(WRITE "${TMP_STDIN}" "neo\n")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_io_read.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 1 "${TMP_STDIN}" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "read:neo")

file(REMOVE_RECURSE "${ROOT_DIR}/build/time_dir_demo")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_time_dirs.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
if(EXISTS "${ROOT_DIR}/build/time_dir_demo")
    message(FATAL_ERROR "expected valid_stdlib_time_dirs binary to remove build/time_dir_demo")
endif()

file(REMOVE "${ROOT_DIR}/build/std_net_fs.txt")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_net_fs.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
if(EXISTS "${ROOT_DIR}/build/std_net_fs.txt")
    message(FATAL_ERROR "expected valid_stdlib_net_fs binary to remove build/std_net_fs.txt")
endif()

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_net_tcp.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_net_udp.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_math_process.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 6 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "6")

file(REMOVE "${ROOT_DIR}/build/path_extra.txt")
file(REMOVE "${ROOT_DIR}/build/path_extra_copy.txt")
file(REMOVE "${ROOT_DIR}/build/path_extra_moved.txt")
cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_path_fs_extra.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
if(EXISTS "${ROOT_DIR}/build/path_extra.txt" OR EXISTS "${ROOT_DIR}/build/path_extra_copy.txt" OR EXISTS "${ROOT_DIR}/build/path_extra_moved.txt")
    message(FATAL_ERROR "expected valid_stdlib_path_fs_extra binary to clean up path/fs demo files")
endif()

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_u8.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "1")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_slice.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 23 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_bytes_text.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 25 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "slice ready!")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_lines.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 32 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "beta")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_stdlib_ipc.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 27 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "{\"tag\":\"ok\",\"text\":\"HELLO\"}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_if_expr.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 1 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_defer.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 0 "" "${TMP_BIN}")
cn_assert_contains("${TMP_RUN}" "cleanup")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_try.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 5 "" "${TMP_BIN}")

cn_run_expect_success("${TMP_VALID}" "${CNEGC_BIN}" build examples/valid_raw_strings.cneg "${TMP_BIN}")
cn_run_binary("${TMP_RUN}" 15 "" "${TMP_BIN}")
