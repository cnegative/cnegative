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

LLVM_AS_BIN=""
if command -v llvm-as-18 >/dev/null 2>&1; then
    LLVM_AS_BIN="llvm-as-18"
elif command -v llvm-as >/dev/null 2>&1; then
    LLVM_AS_BIN="llvm-as"
fi

if [ -z "$LLVM_AS_BIN" ]; then
    printf 'expected llvm-as-18 or llvm-as in PATH for backend smoke tests\n'
    exit 1
fi

./build/cnegc check examples/valid_basic.cneg >"$tmp_valid"
./build/cnegc check examples/valid_structs_arrays.cneg >"$tmp_valid"
./build/cnegc check examples/valid_modules_ptr_result.cneg >"$tmp_valid"
./build/cnegc check examples/valid_imported_structs.cneg >"$tmp_valid"
./build/cnegc check examples/valid_llvm_backend.cneg >"$tmp_valid"
./build/cnegc check examples/valid_strings.cneg >"$tmp_valid"
./build/cnegc check examples/valid_input_equality.cneg >"$tmp_valid"

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

./build/cnegc llvm-ir examples/valid_basic.cneg >"$tmp_ll"
if ! grep -q '@cn_valid_basic__main' "$tmp_ll"; then
    printf 'expected LLVM IR symbol for valid_basic.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

./build/cnegc llvm-ir examples/valid_structs_arrays.cneg >"$tmp_ll"
if ! grep -q '%cn_valid_structs_arrays__User = type' "$tmp_ll"; then
    printf 'expected struct type lowering in LLVM IR for valid_structs_arrays.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

./build/cnegc llvm-ir examples/valid_modules_ptr_result.cneg >"$tmp_ll"
if ! grep -q '@malloc' "$tmp_ll"; then
    printf 'expected malloc lowering in LLVM IR for valid_modules_ptr_result.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

./build/cnegc llvm-ir examples/valid_imported_structs.cneg >"$tmp_ll"
if ! grep -q '%cn_shapes__Point = type' "$tmp_ll"; then
    printf 'expected imported struct lowering in LLVM IR for valid_imported_structs.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

./build/cnegc llvm-ir examples/valid_llvm_backend.cneg >"$tmp_ll"
if ! grep -q '@cn_math__add' "$tmp_ll"; then
    printf 'expected imported module call lowering in LLVM IR for valid_llvm_backend.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

./build/cnegc llvm-ir examples/valid_strings.cneg >"$tmp_ll"
if ! grep -q '@.cn.str.' "$tmp_ll"; then
    printf 'expected string global lowering in LLVM IR for valid_strings.cneg\n'
    cat "$tmp_ll"
    exit 1
fi
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

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
"$LLVM_AS_BIN" "$tmp_ll" -o "$tmp_bc"

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
