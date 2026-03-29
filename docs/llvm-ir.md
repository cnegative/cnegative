# LLVM IR Backend

`cnegative` now has a real LLVM IR backend on top of the structured typed IR.

## CLI

```sh
build/cnegc llvm-ir examples/valid_llvm_backend.cneg
build/cnegc obj examples/valid_basic.cneg
build/cnegc build examples/valid_basic.cneg
```

## Current Supported Lowering

- `int`, `bool`, `str`, arrays, structs, `ptr`, and `result` function/data types.
- Local bindings with mutable reassignment.
- Arithmetic and comparison operators.
- Short-circuit `&&` and `||`.
- `if`, `while`, `loop`, and range `for`.
- Local function calls and imported module function calls.
- Struct literals, array literals, field access, indexing, `alloc`, `addr`, `deref`, `free`, `ok`, `err`, `.ok`, and guarded `.value`.
- Module-level constants lowered after typed IR folding and canonicalization.
- `print(...)`, `input()`, `str_copy(...)`, `str_concat(...)`, and string equality lowered through embedded LLVM runtime helpers backed by libc.
- Object emission and binary linking through `clang-18` or `clang`.
- The emitted module target triple is selected from the host build target instead of being hardcoded to Linux.

## Runtime Notes

- `input()` trims the trailing newline, duplicates the bytes into owned heap storage, and returns that copy as `str`.
- `str_copy(...)` duplicates an existing string into owned heap storage through the generated runtime helper.
- `str_concat(...)` allocates and returns a new owned concatenated string through the generated runtime helper.
- `free some_string;` lowers to a tracked string-free helper so owned runtime strings can be released safely without freeing string literals.

Unsupported lowering reports `E3021` before any LLVM IR text is printed.
