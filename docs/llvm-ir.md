# LLVM IR Backend

`cnegative` now has a real LLVM IR backend on top of the structured typed IR.

## CLI

```sh
build/cnegc llvm-ir examples/valid_llvm_backend.cneg
build/cnegc obj examples/valid_basic.cneg
build/cnegc build examples/valid_basic.cneg
```

## Current Supported Lowering

- `int`, `u8`, `bool`, `str`, arrays, structs, `ptr`, and `result` function/data types.
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
- `std.net.is_ipv4(...)` lowers to a dotted-decimal IPv4 validator, `std.net.join_host_port(...)` lowers to a formatting helper that returns an owned `"host:port"` string, the blocking `std.net.tcp_connect(...)`, `std.net.tcp_listen(...)`, `std.net.accept(...)`, `std.net.send(...)`, `std.net.recv(...)`, and `std.net.close(...)` calls lower to embedded IPv4 TCP runtime helpers, and `std.net.udp_bind(...)`, `std.net.udp_send_to(...)`, and `std.net.udp_recv_from(...)` lower to embedded IPv4 UDP runtime helpers.
- Linux and macOS use POSIX/BSD socket calls in the emitted runtime, while Windows lowers through Winsock and links `ws2_32`.
- `std.net.recv(...)` returns an owned runtime string on success, and successful `std.net.udp_recv_from(...)` results contain owned `host` and `data` strings inside `std.net.UdpPacket`. Current socket/listener handles are surfaced to the language as raw `int` values.
- `std.process.platform(...)` and `std.process.arch(...)` return owned copies of target strings, and `std.process.exit(...)` lowers to a runtime exit helper.
- `u8` lowers as LLVM `i8`, `byte` is parsed as an alias for `u8`, matching `u8` comparisons lower with unsigned predicates, and printing `u8` widens through the runtime print helper.
- `free some_string;` lowers to a tracked string-free helper so owned runtime strings can be released safely without freeing string literals.

Unsupported lowering reports `E3021` before any LLVM IR text is printed.
