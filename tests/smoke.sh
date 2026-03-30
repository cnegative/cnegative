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
./build/cnegc check examples/valid_u8.cneg >"$tmp_valid"

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
if ! grep -q 'std.math.clamp' "$tmp_ir"; then
    printf 'expected std.math.clamp builtin lowering in typed IR for valid_stdlib_math_process.cneg\n'
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

./build/cnegc ir examples/valid_u8.cneg >"$tmp_ir"
if ! grep -q 'fn valid_u8.main() -> u8' "$tmp_ir"; then
    printf 'expected u8 main signature in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'let alias:u8 = 200;' "$tmp_ir"; then
    printf 'expected byte alias canonicalized to u8 in typed IR for valid_u8.cneg\n'
    cat "$tmp_ir"
    exit 1
fi
if ! grep -q 'packet.payload\[1\] > packet.payload\[0\]' "$tmp_ir"; then
    printf 'expected u8 comparison in typed IR for valid_u8.cneg\n'
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

./build/cnegc llvm-ir examples/valid_u8.cneg >"$tmp_ll"
if ! grep -q 'alloca i8' "$tmp_ll"; then
    printf 'expected u8 stack storage lowering in LLVM IR for valid_u8.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'icmp ugt i8' "$tmp_ll"; then
    printf 'expected unsigned u8 comparison lowering in LLVM IR for valid_u8.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
if ! grep -q 'zext i8' "$tmp_ll"; then
    printf 'expected u8 widening in LLVM IR for valid_u8.cneg\n'
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
if [ "$status" -ne 7 ]; then
    printf 'expected valid_stdlib_math_process binary to exit 7, got %d\n' "$status"
    cat "$tmp_run"
    exit 1
fi
if ! grep -q '^7$' "$tmp_run"; then
    printf 'expected valid_stdlib_math_process binary to print 7\n'
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
if ! grep -q '^200$' "$tmp_run"; then
    printf 'expected valid_u8 binary to print 200\n'
    cat "$tmp_run"
    exit 1
fi
