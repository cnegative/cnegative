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
- `std.math`, `std.strings`, `std.parse`, `std.fs`, `std.io`, `std.time`, `std.env`, `std.path`, `std.net`, and `std.process` lowered through embedded LLVM runtime helpers backed by libc.
- Object emission and binary linking through `clang-18` or `clang`.
- The emitted module target triple is selected from the host build target instead of being hardcoded to Linux.

## Runtime Notes

- `input()` trims the trailing newline, duplicates the bytes into owned heap storage, and returns that copy as `str`.
- `str_copy(...)` duplicates an existing string into owned heap storage through the generated runtime helper.
- `str_concat(...)` allocates and returns a new owned concatenated string through the generated runtime helper.
- `std.strings.copy(...)` and `std.strings.concat(...)` lower to the same owned-string helpers as the global forms.
- `std.parse.to_int(...)` and `std.parse.to_bool(...)` lower to runtime parsing helpers that return `result`.
- `std.fs.exists(...)`, `std.fs.cwd(...)`, `std.fs.create_dir(...)`, `std.fs.remove_dir(...)`, `std.fs.read_text(...)`, `std.fs.file_size(...)`, `std.fs.copy(...)`, `std.fs.write_text(...)`, `std.fs.append_text(...)`, `std.fs.rename(...)`, `std.fs.move(...)`, and `std.fs.remove(...)` lower to runtime file helpers built on libc stdio and basic directory APIs.
- `std.io.write(...)` lowers to a string write helper without a newline, `std.io.write_line(...)` lowers to a newline-print helper, and `std.io.read_line(...)` lowers to the same owned-string runtime helper as `input()`.
- `std.time.now_ms(...)` lowers to a millisecond clock helper and `std.time.sleep_ms(...)` lowers to a runtime sleep helper.
- `std.math.abs(...)`, `std.math.min(...)`, `std.math.max(...)`, and `std.math.clamp(...)` lower to small integer runtime helpers.
- `std.env.has(...)` and `std.env.get(...)` lower to runtime environment helpers built on libc `getenv`.
- `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, `std.path.is_absolute(...)`, and `std.path.parent(...)` lower to runtime path helpers that understand both `/` and `\\` separators.
- `std.net.is_ipv4(...)` lowers to a dotted-decimal IPv4 validator and `std.net.join_host_port(...)` lowers to a small formatting helper that returns an owned `"host:port"` string.
- `std.process.platform(...)` and `std.process.arch(...)` return owned copies of target strings, and `std.process.exit(...)` lowers to a runtime exit helper.
- `free some_string;` lowers to a tracked string-free helper so owned runtime strings can be released safely without freeing string literals.

Unsupported lowering reports `E3021` before any LLVM IR text is printed.
