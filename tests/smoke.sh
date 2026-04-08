#!/usr/bin/env bash
set -euo pipefail

tmp_valid=$(mktemp)
tmp_invalid=$(mktemp)
tmp_ir=$(mktemp)
tmp_ll=$(mktemp)
tmp_bc=$(mktemp)
tmp_obj=$(mktemp)
tmp_bin=$(mktemp)
tmp_run=$(mktemp)
trap 'rm -f "$tmp_valid" "$tmp_invalid" "$tmp_ir" "$tmp_ll" "$tmp_bc" "$tmp_obj" "$tmp_bin" "$tmp_run"' EXIT

rm -f "$tmp_obj" "$tmp_bin"
mkdir -p build

LLVM_AS_BIN=""
CLANG_BIN=""
if command -v llvm-as-18 >/dev/null 2>&1; then
    LLVM_AS_BIN="llvm-as-18"
elif command -v llvm-as >/dev/null 2>&1; then
    LLVM_AS_BIN="llvm-as"
fi

if command -v clang-18 >/dev/null 2>&1; then
    CLANG_BIN="clang-18"
elif command -v clang >/dev/null 2>&1; then
    CLANG_BIN="clang"
fi

if [ -z "$LLVM_AS_BIN" ] && [ -z "$CLANG_BIN" ]; then
    printf 'expected llvm-as-18, llvm-as, clang-18, or clang in PATH for backend smoke tests\n'
    exit 1
fi

cn_verify_llvm_ir() {
    local ir_path="$1"
    local output_path="$2"

    if [ -n "$LLVM_AS_BIN" ]; then
        "$LLVM_AS_BIN" "$ir_path" -o "$output_path"
        return
    fi

    "$CLANG_BIN" -c -x ir "$ir_path" -o "$output_path"
}

./build/cnegc check examples/valid_basic.cneg >"$tmp_valid"
./build/cnegc check examples/valid_structs_arrays.cneg >"$tmp_valid"
./build/cnegc check examples/valid_modules_ptr_result.cneg >"$tmp_valid"
./build/cnegc check examples/valid_imported_structs.cneg >"$tmp_valid"
./build/cnegc check examples/valid_llvm_backend.cneg >"$tmp_valid"
./build/cnegc check examples/valid_strings.cneg >"$tmp_valid"
./build/cnegc check examples/valid_input_equality.cneg >"$tmp_valid"
./build/cnegc check examples/valid_consts_strings.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_more.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_io_fs.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_io_read.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_time_dirs.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_net_fs.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_net_tcp.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_net_udp.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_math_process.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_path_fs_extra.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_term.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_term_more.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_term_render.cneg >"$tmp_valid"
./build/cnegc check examples/valid_u8.cneg >"$tmp_valid"
./build/cnegc check examples/valid_slice.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_bytes_text.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_ipc.cneg >"$tmp_valid"
./build/cnegc check examples/valid_stdlib_lines.cneg >"$tmp_valid"
./build/cnegc check examples/valid_if_expr.cneg >"$tmp_valid"
./build/cnegc check examples/valid_defer.cneg >"$tmp_valid"
./build/cnegc check examples/valid_try.cneg >"$tmp_valid"
./build/cnegc check examples/valid_raw_strings.cneg >"$tmp_valid"

./build/cnegc bench-lexer examples/valid_basic.cneg 5 >"$tmp_valid"
if ! grep -q 'tokens_per_second:' "$tmp_valid"; then
    printf 'expected lexer benchmark output\n'
    cat "$tmp_valid"
    exit 1
fi

./build/cnegc ir examples/valid_imported_structs.cneg >"$tmp_ir"
if ! grep -q 'fn valid_imported_structs.main() -> int' "$tmp_ir"; then
    printf 'expected typed IR function signature for valid_imported_structs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

if ! grep -q 'return w.point.y;' "$tmp_ir"; then
    printf 'expected explicit tail return in typed IR for valid_imported_structs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_modules_ptr_result.cneg >"$tmp_ir"
if ! grep -q 'return ok (a / b);' "$tmp_ir"; then
    printf 'expected result expression lowering in typed IR for valid_modules_ptr_result.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'deref px' "$tmp_ir"; then
    printf 'expected deref lowering in typed IR for valid_modules_ptr_result.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_consts_strings.cneg >"$tmp_ir"
if ! grep -q 'const valid_consts_strings.LOCAL:int = 20;' "$tmp_ir"; then
    printf 'expected optimized local constant in typed IR for valid_consts_strings.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'if true {' "$tmp_ir"; then
    printf 'expected folded constant condition in typed IR for valid_consts_strings.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'return 20;' "$tmp_ir"; then
    printf 'expected folded return value in typed IR for valid_consts_strings.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib.cneg >"$tmp_ir"
if ! grep -q 'std.strings.concat' "$tmp_ir"; then
    printf 'expected std.strings.concat builtin lowering in typed IR for valid_stdlib.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.fs.read_text' "$tmp_ir"; then
    printf 'expected std.fs.read_text builtin lowering in typed IR for valid_stdlib.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_more.cneg >"$tmp_ir"
if ! grep -q 'std.env.get' "$tmp_ir"; then
    printf 'expected std.env.get builtin lowering in typed IR for valid_stdlib_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.path.join' "$tmp_ir"; then
    printf 'expected std.path.join builtin lowering in typed IR for valid_stdlib_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_io_fs.cneg >"$tmp_ir"
if ! grep -q 'std.io.write_line' "$tmp_ir"; then
    printf 'expected std.io.write_line builtin lowering in typed IR for valid_stdlib_io_fs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.fs.append_text' "$tmp_ir"; then
    printf 'expected std.fs.append_text builtin lowering in typed IR for valid_stdlib_io_fs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_time_dirs.cneg >"$tmp_ir"
if ! grep -q 'std.time.now_ms' "$tmp_ir"; then
    printf 'expected std.time.now_ms builtin lowering in typed IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.fs.create_dir' "$tmp_ir"; then
    printf 'expected std.fs.create_dir builtin lowering in typed IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_net_fs.cneg >"$tmp_ir"
if ! grep -q 'std.net.join_host_port' "$tmp_ir"; then
    printf 'expected std.net.join_host_port builtin lowering in typed IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.fs.file_size' "$tmp_ir"; then
    printf 'expected std.fs.file_size builtin lowering in typed IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_net_tcp.cneg >"$tmp_ir"
if ! grep -q 'std.net.tcp_connect' "$tmp_ir"; then
    printf 'expected std.net.tcp_connect builtin lowering in typed IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.net.tcp_listen' "$tmp_ir"; then
    printf 'expected std.net.tcp_listen builtin lowering in typed IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.net.recv' "$tmp_ir"; then
    printf 'expected std.net.recv builtin lowering in typed IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_net_udp.cneg >"$tmp_ir"
if ! grep -q 'std.net.udp_bind' "$tmp_ir"; then
    printf 'expected std.net.udp_bind builtin lowering in typed IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.net.udp_send_to' "$tmp_ir"; then
    printf 'expected std.net.udp_send_to builtin lowering in typed IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.net.udp_recv_from' "$tmp_ir"; then
    printf 'expected std.net.udp_recv_from builtin lowering in typed IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'result std.net.UdpPacket' "$tmp_ir"; then
    printf 'expected std.net.UdpPacket result lowering in typed IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_math_process.cneg >"$tmp_ir"
if ! grep -q '%' "$tmp_ir"; then
    printf 'expected modulo operator lowering in typed IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.math.clamp' "$tmp_ir"; then
    printf 'expected std.math.clamp builtin lowering in typed IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.math.gcd' "$tmp_ir"; then
    printf 'expected std.math.gcd builtin lowering in typed IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.math.between' "$tmp_ir"; then
    printf 'expected std.math.between builtin lowering in typed IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.process.platform' "$tmp_ir"; then
    printf 'expected std.process.platform builtin lowering in typed IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_path_fs_extra.cneg >"$tmp_ir"
if ! grep -q 'std.fs.copy' "$tmp_ir"; then
    printf 'expected std.fs.copy builtin lowering in typed IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.path.extension' "$tmp_ir"; then
    printf 'expected std.path.extension builtin lowering in typed IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_term.cneg >"$tmp_ir"
if ! grep -q 'std.term.columns' "$tmp_ir"; then
    printf 'expected std.term.columns builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.enter_raw_mode' "$tmp_ir"; then
    printf 'expected std.term.enter_raw_mode builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.read_byte' "$tmp_ir"; then
    printf 'expected std.term.read_byte builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.read_event' "$tmp_ir"; then
    printf 'expected std.term.read_event builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.read_paste' "$tmp_ir"; then
    printf 'expected std.term.read_paste builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.enable_mouse' "$tmp_ir"; then
    printf 'expected std.term.enable_mouse builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.enable_bracketed_paste' "$tmp_ir"; then
    printf 'expected std.term.enable_bracketed_paste builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.enter_alt_screen' "$tmp_ir"; then
    printf 'expected std.term.enter_alt_screen builtin lowering in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'result std.term.Event' "$tmp_ir"; then
    printf 'expected std.term.Event result type in typed IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_term_more.cneg >"$tmp_ir"
if ! grep -q 'std.term.term_name' "$tmp_ir"; then
    printf 'expected std.term.term_name builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.supports_truecolor' "$tmp_ir"; then
    printf 'expected std.term.supports_truecolor builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.read_event_timeout' "$tmp_ir"; then
    printf 'expected std.term.read_event_timeout builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.rgb' "$tmp_ir"; then
    printf 'expected std.term.rgb builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.string_width' "$tmp_ir"; then
    printf 'expected std.term.string_width builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.set_scroll_region' "$tmp_ir"; then
    printf 'expected std.term.set_scroll_region builtin lowering in typed IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_term_render.cneg >"$tmp_ir"
if ! grep -q 'std.term.buffer_new' "$tmp_ir"; then
    printf 'expected std.term.buffer_new builtin lowering in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.buffer_resize' "$tmp_ir"; then
    printf 'expected std.term.buffer_resize builtin lowering in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.render_diff_clip' "$tmp_ir"; then
    printf 'expected std.term.render_diff_clip builtin lowering in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'result ptr std.term.Buffer' "$tmp_ir"; then
    printf 'expected std.term.Buffer pointer result type in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'term.Clip' "$tmp_ir"; then
    printf 'expected std.term Clip use in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.term.codepoint_width' "$tmp_ir"; then
    printf 'expected std.term.codepoint_width builtin lowering in typed IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_u8.cneg >"$tmp_ir"
if ! grep -q 'fn valid_u8.main() -> u8' "$tmp_ir"; then
    printf 'expected u8 main signature in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'return if (value == 0) {' "$tmp_ir"; then
    printf 'expected if-expression lowering in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'packet.kind == 42' "$tmp_ir"; then
    printf 'expected byte literal comparison in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'packet.payload\[1\] == 1' "$tmp_ir"; then
    printf 'expected u8 equality with literal in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_slice.cneg >"$tmp_ir"
if ! grep -q 'fn valid_slice.sum_head(xs:slice int) -> int' "$tmp_ir"; then
    printf 'expected slice parameter lowering in typed IR for valid_slice.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'let view:slice int = slice data;' "$tmp_ir"; then
    printf 'expected array-to-slice coercion in typed IR for valid_slice.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'let middle:slice int = view\[1..3\];' "$tmp_ir"; then
    printf 'expected explicit subslice lowering in typed IR for valid_slice.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'let prefix:slice int = data\[0..2\];' "$tmp_ir"; then
    printf 'expected omitted-start slice lowering in typed IR for valid_slice.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'third(data\[1..4\])' "$tmp_ir"; then
    printf 'expected omitted-end slice lowering in typed IR for valid_slice.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_bytes_text.cneg >"$tmp_ir"
if ! grep -q 'std.bytes.append' "$tmp_ir"; then
    printf 'expected std.bytes.append builtin lowering in typed IR for valid_stdlib_bytes_text.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'let view:slice u8 = std.bytes.view(buffer);' "$tmp_ir"; then
    printf 'expected std.bytes slice view lowering in typed IR for valid_stdlib_bytes_text.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.text.build' "$tmp_ir"; then
    printf 'expected std.text.build builtin lowering in typed IR for valid_stdlib_bytes_text.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_lines.cneg >"$tmp_ir"
if ! grep -q 'std.lines.insert' "$tmp_ir"; then
    printf 'expected std.lines.insert builtin lowering in typed IR for valid_stdlib_lines.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.lines.get' "$tmp_ir"; then
    printf 'expected std.lines.get builtin lowering in typed IR for valid_stdlib_lines.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'result ptr std.lines.Buffer' "$tmp_ir"; then
    printf 'expected std.lines buffer result type in typed IR for valid_stdlib_lines.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_stdlib_ipc.cneg >"$tmp_ir"
if ! grep -q 'std.ipc.spawn' "$tmp_ir"; then
    printf 'expected std.ipc.spawn builtin lowering in typed IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.ipc.request_line' "$tmp_ir"; then
    printf 'expected std.ipc.request_line builtin lowering in typed IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'std.ipc.stderr_read_line' "$tmp_ir"; then
    printf 'expected std.ipc.stderr_read_line builtin lowering in typed IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'result ptr std.ipc.Child' "$tmp_ir"; then
    printf 'expected std.ipc child result type in typed IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_if_expr.cneg >"$tmp_ir"
if ! grep -q 'let picked:int = if flag {' "$tmp_ir"; then
    printf 'expected if-expression binding in typed IR for valid_if_expr.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'return if (value == 0) {' "$tmp_ir"; then
    printf 'expected nested if-expression return in typed IR for valid_if_expr.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_defer.cneg >"$tmp_ir"
if ! grep -q 'print("cleanup");' "$tmp_ir"; then
    printf 'expected defer lowering to emit cleanup in typed IR for valid_defer.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'if (value < 0) {' "$tmp_ir"; then
    printf 'expected early-return branch in typed IR for valid_defer.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc ir examples/valid_try.cneg >"$tmp_ir"
if ! grep -q 'let __cn_try_' "$tmp_ir"; then
    printf 'expected try lowering to create a temporary result binding in typed IR for valid_try.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'if !__cn_try_' "$tmp_ir"; then
    printf 'expected try lowering to branch on .ok in typed IR for valid_try.cneg\n'
    cat "$tmp_ir"
    exit 1
fi

./build/cnegc llvm-ir examples/valid_basic.cneg >"$tmp_ll"
if ! grep -q '@cn_valid_basic__main' "$tmp_ll"; then
    printf 'expected LLVM IR symbol for valid_basic.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_structs_arrays.cneg >"$tmp_ll"
if ! grep -q '%cn_valid_structs_arrays__User = type' "$tmp_ll"; then
    printf 'expected struct type lowering in LLVM IR for valid_structs_arrays.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_modules_ptr_result.cneg >"$tmp_ll"
if ! grep -q '@malloc' "$tmp_ll"; then
    printf 'expected malloc lowering in LLVM IR for valid_modules_ptr_result.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_imported_structs.cneg >"$tmp_ll"
if ! grep -q '%cn_shapes__Point = type' "$tmp_ll"; then
    printf 'expected imported struct lowering in LLVM IR for valid_imported_structs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_llvm_backend.cneg >"$tmp_ll"
if ! grep -q '@cn_math__add' "$tmp_ll"; then
    printf 'expected imported module call lowering in LLVM IR for valid_llvm_backend.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_strings.cneg >"$tmp_ll"
if ! grep -q '@.cn.str.' "$tmp_ll"; then
    printf 'expected string global lowering in LLVM IR for valid_strings.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_input_equality.cneg >"$tmp_ll"
if ! grep -q '@cn_input' "$tmp_ll"; then
    printf 'expected input runtime lowering in LLVM IR for valid_input_equality.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_free_str' "$tmp_ll"; then
    printf 'expected owned string free helper in LLVM IR for valid_input_equality.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@strcmp' "$tmp_ll"; then
    printf 'expected string equality lowering in LLVM IR for valid_input_equality.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_consts_strings.cneg >"$tmp_ll"
if ! grep -q '@cn_concat_str' "$tmp_ll"; then
    printf 'expected string concat runtime helper in LLVM IR for valid_consts_strings.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'call ptr @cn_dup_cstr' "$tmp_ll"; then
    printf 'expected owned string copy lowering in LLVM IR for valid_consts_strings.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib.cneg >"$tmp_ll"
if ! grep -q '@cn_parse_int' "$tmp_ll"; then
    printf 'expected parse runtime helper lowering in LLVM IR for valid_stdlib.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_read_text' "$tmp_ll"; then
    printf 'expected fs runtime helper lowering in LLVM IR for valid_stdlib.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_starts_with' "$tmp_ll"; then
    printf 'expected string prefix helper lowering in LLVM IR for valid_stdlib.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_more.cneg >"$tmp_ll"
if ! grep -q '@cn_env_get' "$tmp_ll"; then
    printf 'expected env getter lowering in LLVM IR for valid_stdlib_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_path_join' "$tmp_ll"; then
    printf 'expected path join lowering in LLVM IR for valid_stdlib_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_remove' "$tmp_ll"; then
    printf 'expected fs remove lowering in LLVM IR for valid_stdlib_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_io_fs.cneg >"$tmp_ll"
if ! grep -q '@cn_write_str' "$tmp_ll"; then
    printf 'expected std.io write lowering in LLVM IR for valid_stdlib_io_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_append_text' "$tmp_ll"; then
    printf 'expected fs append lowering in LLVM IR for valid_stdlib_io_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_rename' "$tmp_ll"; then
    printf 'expected fs rename lowering in LLVM IR for valid_stdlib_io_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_time_dirs.cneg >"$tmp_ll"
if ! grep -q '@cn_time_now_ms' "$tmp_ll"; then
    printf 'expected time now lowering in LLVM IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_time_sleep_ms' "$tmp_ll"; then
    printf 'expected time sleep lowering in LLVM IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_create_dir' "$tmp_ll"; then
    printf 'expected fs create_dir lowering in LLVM IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_remove_dir' "$tmp_ll"; then
    printf 'expected fs remove_dir lowering in LLVM IR for valid_stdlib_time_dirs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_net_fs.cneg >"$tmp_ll"
if ! grep -q '@cn_net_is_ipv4' "$tmp_ll"; then
    printf 'expected std.net IPv4 lowering in LLVM IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_join_host_port' "$tmp_ll"; then
    printf 'expected std.net join_host_port lowering in LLVM IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_file_size' "$tmp_ll"; then
    printf 'expected fs file_size lowering in LLVM IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_fs_cwd' "$tmp_ll"; then
    printf 'expected fs cwd lowering in LLVM IR for valid_stdlib_net_fs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_net_tcp.cneg >"$tmp_ll"
if ! grep -q '@cn_net_tcp_connect' "$tmp_ll"; then
    printf 'expected std.net tcp_connect lowering in LLVM IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_tcp_listen' "$tmp_ll"; then
    printf 'expected std.net tcp_listen lowering in LLVM IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_send' "$tmp_ll"; then
    printf 'expected std.net send lowering in LLVM IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_recv' "$tmp_ll"; then
    printf 'expected std.net recv lowering in LLVM IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_accept' "$tmp_ll"; then
    printf 'expected std.net accept lowering in LLVM IR for valid_stdlib_net_tcp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_net_udp.cneg >"$tmp_ll"
if ! grep -q '%cn_std_x2E_net__UdpPacket = type' "$tmp_ll"; then
    printf 'expected std.net.UdpPacket struct lowering in LLVM IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_udp_bind' "$tmp_ll"; then
    printf 'expected std.net udp_bind lowering in LLVM IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_udp_send_to' "$tmp_ll"; then
    printf 'expected std.net udp_send_to lowering in LLVM IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_net_udp_recv_from' "$tmp_ll"; then
    printf 'expected std.net udp_recv_from lowering in LLVM IR for valid_stdlib_net_udp.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_math_process.cneg >"$tmp_ll"
if ! grep -q '@cn_math_clamp' "$tmp_ll"; then
    printf 'expected std.math clamp lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_math_gcd' "$tmp_ll"; then
    printf 'expected std.math gcd lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_math_between' "$tmp_ll"; then
    printf 'expected std.math between lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'srem ' "$tmp_ll"; then
    printf 'expected modulo lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_process_platform' "$tmp_ll"; then
    printf 'expected std.process platform lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_process_exit' "$tmp_ll"; then
    printf 'expected std.process exit lowering in LLVM IR for valid_stdlib_math_process.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_path_fs_extra.cneg >"$tmp_ll"
if ! grep -q '@cn_fs_copy' "$tmp_ll"; then
    printf 'expected std.fs copy lowering in LLVM IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_path_extension' "$tmp_ll"; then
    printf 'expected std.path extension lowering in LLVM IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_path_stem' "$tmp_ll"; then
    printf 'expected std.path stem lowering in LLVM IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_path_is_absolute' "$tmp_ll"; then
    printf 'expected std.path is_absolute lowering in LLVM IR for valid_stdlib_path_fs_extra.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_term.cneg >"$tmp_ll"
if ! grep -q '@cn_term_columns' "$tmp_ll"; then
    printf 'expected std.term columns lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_enter_raw_mode' "$tmp_ll"; then
    printf 'expected std.term raw-mode lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_read_byte' "$tmp_ll"; then
    printf 'expected std.term byte-read lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_read_event' "$tmp_ll"; then
    printf 'expected std.term event-read lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_read_paste' "$tmp_ll"; then
    printf 'expected std.term paste-read lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_enable_mouse' "$tmp_ll"; then
    printf 'expected std.term mouse enable lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_enable_bracketed_paste' "$tmp_ll"; then
    printf 'expected std.term bracketed paste lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_move_cursor' "$tmp_ll"; then
    printf 'expected std.term cursor lowering in LLVM IR for valid_stdlib_term.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_term_more.cneg >"$tmp_ll"
if ! grep -q '@cn_term_term_name' "$tmp_ll"; then
    printf 'expected std.term.term_name runtime helper in LLVM IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_supports_truecolor' "$tmp_ll"; then
    printf 'expected std.term.supports_truecolor runtime helper in LLVM IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_read_event_timeout' "$tmp_ll"; then
    printf 'expected std.term.read_event_timeout runtime helper in LLVM IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_rgb' "$tmp_ll"; then
    printf 'expected std.term.rgb runtime helper in LLVM IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_set_scroll_region' "$tmp_ll"; then
    printf 'expected std.term.set_scroll_region runtime helper in LLVM IR for valid_stdlib_term_more.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_term_render.cneg >"$tmp_ll"
if ! grep -q '@cn_term_set_style' "$tmp_ll"; then
    printf 'expected std.term style lowering in LLVM IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_buffer_new' "$tmp_ll"; then
    printf 'expected std.term buffer allocation lowering in LLVM IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_buffer_resize' "$tmp_ll"; then
    printf 'expected std.term buffer resize lowering in LLVM IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_term_render_diff_clip' "$tmp_ll"; then
    printf 'expected std.term clipped render diff lowering in LLVM IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '%cn_std_x2E_term__Clip = type' "$tmp_ll"; then
    printf 'expected std.term Clip type lowering in LLVM IR for valid_stdlib_term_render.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_u8.cneg >"$tmp_ll"
if ! grep -q 'alloca i8' "$tmp_ll"; then
    printf 'expected u8 stack storage lowering in LLVM IR for valid_u8.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'icmp eq i8' "$tmp_ll"; then
    printf 'expected unsigned u8 equality lowering in LLVM IR for valid_u8.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'zext i8' "$tmp_ll"; then
    printf 'expected u8 widening in LLVM IR for valid_u8.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_slice.cneg >"$tmp_ll"
if ! grep -q '{ ptr, i64 }' "$tmp_ll"; then
    printf 'expected slice aggregate lowering in LLVM IR for valid_slice.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'extractvalue { ptr, i64 }' "$tmp_ll"; then
    printf 'expected slice field extraction in LLVM IR for valid_slice.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'sub i64' "$tmp_ll"; then
    printf 'expected slice length subtraction in LLVM IR for valid_slice.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_bytes_text.cneg >"$tmp_ll"
if ! grep -q '@cn_bytes_append' "$tmp_ll"; then
    printf 'expected std.bytes append runtime lowering in LLVM IR for valid_stdlib_bytes_text.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_text_build' "$tmp_ll"; then
    printf 'expected std.text build runtime lowering in LLVM IR for valid_stdlib_bytes_text.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_lines.cneg >"$tmp_ll"
if ! grep -q '@cn_lines_insert' "$tmp_ll"; then
    printf 'expected std.lines insert runtime lowering in LLVM IR for valid_stdlib_lines.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_lines_get' "$tmp_ll"; then
    printf 'expected std.lines get runtime lowering in LLVM IR for valid_stdlib_lines.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_stdlib_ipc.cneg >"$tmp_ll"
if ! grep -q '@cn_ipc_spawn' "$tmp_ll"; then
    printf 'expected std.ipc spawn runtime lowering in LLVM IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_ipc_native_spawn' "$tmp_ll"; then
    printf 'expected std.ipc native spawn declaration in LLVM IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_ipc_request_line' "$tmp_ll"; then
    printf 'expected std.ipc request line runtime lowering in LLVM IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q '@cn_ipc_stdout_read_line' "$tmp_ll"; then
    printf 'expected std.ipc stdout line runtime lowering in LLVM IR for valid_stdlib_ipc.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

./build/cnegc llvm-ir examples/valid_if_expr.cneg >"$tmp_ll"
if ! grep -q 'br i1' "$tmp_ll"; then
    printf 'expected branch lowering in LLVM IR for valid_if_expr.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'alloca i64' "$tmp_ll"; then
    printf 'expected if-expression result storage in LLVM IR for valid_if_expr.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
cn_verify_llvm_ir "$tmp_ll" "$tmp_bc"

if ./build/cnegc check examples/invalid_if_int_condition.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_if_int_condition.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3005' "$tmp_invalid"; then
    printf 'expected E3005 in invalid_if_int_condition.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_if_expr_int_condition.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_if_expr_int_condition.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3005' "$tmp_invalid"; then
    printf 'expected E3005 in invalid_if_expr_int_condition.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_if_expr_void.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_if_expr_void.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3029' "$tmp_invalid"; then
    printf 'expected E3029 in invalid_if_expr_void.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_if_expr_branch_type.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_if_expr_branch_type.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3030' "$tmp_invalid"; then
    printf 'expected E3030 in invalid_if_expr_branch_type.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_addr_target.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_addr_target.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3031' "$tmp_invalid"; then
    printf 'expected E3031 in invalid_addr_target.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_deref_non_ptr.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_deref_non_ptr.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3032' "$tmp_invalid"; then
    printf 'expected E3032 in invalid_deref_non_ptr.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_assignment_target.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_assignment_target.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E1006' "$tmp_invalid"; then
    printf 'expected E1006 in invalid_assignment_target.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_struct_missing_field.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_struct_missing_field.cneg to fail\n'
    exit 1
fi

if ! grep -q 'missing field' "$tmp_invalid"; then
    printf 'expected missing field diagnostic in invalid_struct_missing_field.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_missing_semicolon.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_missing_semicolon.cneg to fail\n'
    exit 1
fi

if ! grep -q "expected ';'" "$tmp_invalid"; then
    printf 'expected semicolon diagnostic in invalid_missing_semicolon.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_unimported_type.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_unimported_type.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3012' "$tmp_invalid"; then
    printf 'expected E3012 in invalid_unimported_type.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_private_import_type.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_private_import_type.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3012' "$tmp_invalid"; then
    printf 'expected E3012 in invalid_private_import_type.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_public_private_type.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_public_private_type.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3023' "$tmp_invalid"; then
    printf 'expected E3023 in invalid_public_private_type.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_result_value_guard.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_result_value_guard.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3024' "$tmp_invalid"; then
    printf 'expected E3024 in invalid_result_value_guard.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_implicit_return.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_implicit_return.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3007' "$tmp_invalid"; then
    printf 'expected E3007 in invalid_implicit_return.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_const_runtime.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_const_runtime.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3025' "$tmp_invalid"; then
    printf 'expected E3025 in invalid_const_runtime.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_const_cycle.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_const_cycle.cneg to fail\n'
    exit 1
fi

if ! grep -q 'E3026' "$tmp_invalid"; then
    printf 'expected E3026 in invalid_const_cycle.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_parse_recovery.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_parse_recovery.cneg to fail\n'
    exit 1
fi

if [ "$(grep -c "expected ';'" "$tmp_invalid")" -lt 4 ]; then
    printf 'expected parser recovery to report multiple semicolon errors in invalid_parse_recovery.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_u8_range.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_u8_range.cneg to fail\n'
    exit 1
fi

if ! grep -q 'u8 literal out of range' "$tmp_invalid"; then
    printf 'expected u8 range diagnostic in invalid_u8_range.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi
if ! grep -q 'E3028' "$tmp_invalid"; then
    printf 'expected E3028 in invalid_u8_range.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

if ./build/cnegc check examples/invalid_try_non_result.cneg >"$tmp_invalid" 2>&1; then
    printf 'expected invalid_try_non_result.cneg to fail\n'
    exit 1
fi
if ! grep -q 'E3033' "$tmp_invalid"; then
    printf 'expected E3033 in invalid_try_non_result.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi
if ! grep -q 'E3034' "$tmp_invalid"; then
    printf 'expected E3034 in invalid_try_non_result.cneg output\n'
    cat "$tmp_invalid"
    exit 1
fi

./build/cnegc obj examples/valid_basic.cneg "$tmp_obj" >"$tmp_valid"
if [ ! -s "$tmp_obj" ]; then
    printf 'expected object output for valid_basic.cneg\n'
    exit 1
fi

./build/cnegc build examples/valid_structs_arrays.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 200 ]; then
    printf 'expected valid_structs_arrays binary to exit 200, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_modules_ptr_result.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 5 ]; then
    printf 'expected valid_modules_ptr_result binary to exit 5, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_llvm_backend.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 3 ]; then
    printf 'expected valid_llvm_backend binary to exit 3, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^3$' "$tmp_run"; then
    printf 'expected valid_llvm_backend binary to print 3\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_strings.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_strings binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^hello from cnegative$' "$tmp_run"; then
    printf 'expected valid_strings binary to print hello from cnegative\n'
    cat "$tmp_run"
    exit 1
fi

printf 'neo\n' >"$tmp_valid"
./build/cnegc build examples/valid_input_equality.cneg "$tmp_bin" >"$tmp_run"
set +e
"$tmp_bin" <"$tmp_valid" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 1 ]; then
    printf 'expected valid_input_equality binary to exit 1, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^neo$' "$tmp_run"; then
    printf 'expected valid_input_equality binary to echo neo\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_consts_strings.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 20 ]; then
    printf 'expected valid_consts_strings binary to exit 20, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^hello world$' "$tmp_run"; then
    printf 'expected valid_consts_strings binary to print hello world\n'
    cat "$tmp_run"
    exit 1
fi

rm -f build/std_demo.txt
./build/cnegc build examples/valid_stdlib.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if [ ! -f build/std_demo.txt ]; then
    printf 'expected valid_stdlib binary to create build/std_demo.txt\n'
    exit 1
fi
if [ "$(cat build/std_demo.txt)" != "42" ]; then
    printf 'expected valid_stdlib binary to write 42 into build/std_demo.txt\n'
    cat build/std_demo.txt
    exit 1
fi

rm -f build/stdlib_more.txt
./build/cnegc build examples/valid_stdlib_more.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_more binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if [ -f build/stdlib_more.txt ]; then
    printf 'expected valid_stdlib_more binary to remove build/stdlib_more.txt\n'
    exit 1
fi

rm -f build/io_fs_demo.txt build/io_fs_demo_renamed.txt
./build/cnegc build examples/valid_stdlib_io_fs.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_io_fs binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^io:ready$' "$tmp_run"; then
    printf 'expected valid_stdlib_io_fs binary to print io:ready\n'
    cat "$tmp_run"
    exit 1
fi
if [ -f build/io_fs_demo.txt ] || [ -f build/io_fs_demo_renamed.txt ]; then
    printf 'expected valid_stdlib_io_fs binary to clean up io demo files\n'
    exit 1
fi

printf 'neo\n' >"$tmp_valid"
./build/cnegc build examples/valid_stdlib_io_read.cneg "$tmp_bin" >"$tmp_run"
set +e
"$tmp_bin" <"$tmp_valid" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 1 ]; then
    printf 'expected valid_stdlib_io_read binary to exit 1, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^read:neo$' "$tmp_run"; then
    printf 'expected valid_stdlib_io_read binary to print read:neo\n'
    cat "$tmp_run"
    exit 1
fi

rm -rf build/time_dir_demo
./build/cnegc build examples/valid_stdlib_time_dirs.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_time_dirs binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if [ -d build/time_dir_demo ]; then
    printf 'expected valid_stdlib_time_dirs binary to remove build/time_dir_demo\n'
    exit 1
fi

rm -f build/std_net_fs.txt
./build/cnegc build examples/valid_stdlib_net_fs.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_net_fs binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if [ -f build/std_net_fs.txt ]; then
    printf 'expected valid_stdlib_net_fs binary to remove build/std_net_fs.txt\n'
    exit 1
fi

./build/cnegc build examples/valid_stdlib_net_tcp.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_net_tcp binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_stdlib_net_udp.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_net_udp binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_stdlib_math_process.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 6 ]; then
    printf 'expected valid_stdlib_math_process binary to exit 6, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^6$' "$tmp_run"; then
    printf 'expected valid_stdlib_math_process binary to print 6\n'
    cat "$tmp_run"
    exit 1
fi

rm -f build/path_extra.txt build/path_extra_copy.txt build/path_extra_moved.txt
./build/cnegc build examples/valid_stdlib_path_fs_extra.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_stdlib_path_fs_extra binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if [ -f build/path_extra.txt ] || [ -f build/path_extra_copy.txt ] || [ -f build/path_extra_moved.txt ]; then
    printf 'expected valid_stdlib_path_fs_extra binary to clean up path/fs demo files\n'
    exit 1
fi

./build/cnegc build examples/valid_u8.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_u8 binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^1$' "$tmp_run"; then
    printf 'expected valid_u8 binary to print 1\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_slice.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 23 ]; then
    printf 'expected valid_slice binary to exit 23, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_stdlib_bytes_text.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 25 ]; then
    printf 'expected valid_stdlib_bytes_text binary to exit 25, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q 'slice ready!' "$tmp_run"; then
    printf 'expected valid_stdlib_bytes_text binary to print built text output\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_stdlib_lines.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 32 ]; then
    printf 'expected valid_stdlib_lines binary to exit 32, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^beta$' "$tmp_run"; then
    printf 'expected valid_stdlib_lines binary to print borrowed line output\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_stdlib_ipc.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 27 ]; then
    printf 'expected valid_stdlib_ipc binary to exit 27, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^{"tag":"ok","text":"HELLO"}$' "$tmp_run"; then
    printf 'expected valid_stdlib_ipc binary to print json-line child output\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_if_expr.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 1 ]; then
    printf 'expected valid_if_expr binary to exit 1, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_defer.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 0 ]; then
    printf 'expected valid_defer binary to exit 0, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^cleanup$' "$tmp_run"; then
    printf 'expected valid_defer binary to print deferred cleanup output\n'
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_try.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 5 ]; then
    printf 'expected valid_try binary to exit 5, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi

./build/cnegc build examples/valid_raw_strings.cneg "$tmp_bin" >"$tmp_valid"
set +e
"$tmp_bin" >"$tmp_run"
status=$?
set -e
if [ "$status" -ne 15 ]; then
    printf 'expected valid_raw_strings binary to exit 15, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
