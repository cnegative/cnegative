# LLVM IR Backend

`cnegative` now has a real LLVM IR backend on top of the structured typed IR.

## CLI

```sh
build/cnegc llvm-ir examples/valid_llvm_backend.cneg
build/cnegc obj examples/valid_basic.cneg
build/cnegc build examples/valid_basic.cneg
```

## Current Supported Lowering

- `int`, `u8`, `bool`, `str`, arrays, slices, structs, `ptr`, and `result` function/data types.
- Local bindings with mutable reassignment.
- Arithmetic and comparison operators, including integer `%`.
- Short-circuit `&&` and `||`.
- `if`, `while`, `loop`, and range `for`.
- Local function calls and imported module function calls.
- Struct literals, array literals, field access, indexing, `alloc`, `addr`, `deref`, `free`, `ok`, `err`, `.ok`, and guarded `.value`.
- Module-level constants lowered after typed IR folding and canonicalization.
- `print(...)`, `input()`, `str_copy(...)`, `str_concat(...)`, and string equality lowered through embedded LLVM runtime helpers backed by libc.
- `std.math`, `std.bytes`, `std.ipc`, `std.lines`, `std.strings`, `std.text`, `std.parse`, `std.fs`, `std.io`, `std.term`, `std.time`, `std.env`, `std.path`, `std.net`, `std.process`, and the experimental Linux-only `std.x11` lowered through embedded LLVM runtime helpers backed by libc or the host platform APIs.
- Object emission and binary linking through `clang-18` or `clang`.
- The emitted module target triple is selected from the host build target instead of being hardcoded to Linux.

## Runtime Notes

- `input()` trims the trailing newline, duplicates the bytes into owned heap storage, and returns that copy as `str`.
- `str_copy(...)` duplicates an existing string into owned heap storage through the generated runtime helper.
- `str_concat(...)` allocates and returns a new owned concatenated string through the generated runtime helper.
- `std.bytes` lowers to growable byte-buffer helpers with runtime allocation, resize-on-append growth, indexed byte reads/writes, and non-owning slice views of current contents.
- `std.ipc` lowers in two layers: thin LLVM wrappers remain in the emitted module, while an embedded native helper object is compiled and linked automatically when the program uses `std.ipc`. That helper owns cross-platform child-process spawning, stdin/stdout/stderr pipes, blocking text reads/writes, blocking line reads/writes for newline-delimited protocols, and a thin request-line wrapper for one-request/one-response tools, plus wait, kill, and release.
- `std.lines` lowers to growable line-buffer helpers that own duplicated line strings internally. `get(...)` returns a borrowed `str` view into that storage, while `set(...)`, `push(...)`, `insert(...)`, and `remove(...)` lower to embedded line-management helpers.
- `std.strings.copy(...)` and `std.strings.concat(...)` lower to the same owned-string helpers as the global forms.
- `std.text` lowers to builder helpers layered on top of the byte-buffer runtime. `std.text.append(...)` copies string bytes into the builder, `std.text.push_byte(...)` appends one byte, `std.text.view(...)` returns a non-owning slice, and `std.text.build(...)` returns an owned runtime string on success.
- `std.parse.to_int(...)` and `std.parse.to_bool(...)` lower to runtime parsing helpers that return `result`.
- `std.fs.exists(...)`, `std.fs.cwd(...)`, `std.fs.create_dir(...)`, `std.fs.remove_dir(...)`, `std.fs.read_text(...)`, `std.fs.file_size(...)`, `std.fs.copy(...)`, `std.fs.write_text(...)`, `std.fs.append_text(...)`, `std.fs.rename(...)`, `std.fs.move(...)`, and `std.fs.remove(...)` lower to runtime file helpers built on libc stdio and basic directory APIs.
- `std.io.write(...)` lowers to a string write helper without a newline, `std.io.write_line(...)` lowers to a newline-print helper, and `std.io.read_line(...)` lowers to the same owned-string runtime helper as `input()`.
- `std.term.is_tty(...)`, `std.term.columns(...)`, `std.term.rows(...)`, `std.term.term_name(...)`, terminal capability queries, `std.term.read_byte(...)`, `std.term.read_byte_timeout(...)`, `std.term.read_event(...)`, `std.term.read_event_timeout(...)`, `std.term.read_paste(...)`, `std.term.write(...)`, `std.term.flush(...)`, `std.term.clear(...)`, line erase helpers, `std.term.move_cursor(...)`, save/restore cursor, cursor visibility toggles, alternate-screen toggles, scroll-region helpers, raw-mode enter/leave, mouse/paste mode toggles, `std.term.rgb(...)`, `std.term.codepoint_width(...)`, `std.term.string_width(...)`, `std.term.decode_codepoint(...)`, `std.term.next_codepoint_offset(...)`, `std.term.set_style(...)`, `std.term.reset_style(...)`, `std.term.buffer_new(...)`, `std.term.buffer_resize(...)`, `std.term.buffer_free(...)`, `std.term.buffer_clear(...)`, `std.term.buffer_set(...)`, `std.term.buffer_get(...)`, `std.term.render_diff(...)`, and `std.term.render_diff_clip(...)` all lower to embedded terminal helpers. `std.term.read_event(...)` normalizes key, mouse, resize, and paste-start input into `std.term.Event { kind, code, modifiers, row, column }`, and `read_event_timeout(...)` exposes the same surface for polling loops. On Unix, resize polling is now signal-backed through `SIGWINCH` before the runtime re-queries rows and columns. `std.term.read_paste(...)` and `std.term.term_name(...)` return owned runtime strings on success. `std.term.render_diff(...)` compares a desired back buffer against a front buffer, paints only changed cells, reuses the current cursor position across adjacent changed cells, then copies the rendered cells into the front buffer, while `render_diff_clip(...)` limits that work to a `std.term.Clip` region. The runtime now auto-detects wide codepoints during `buffer_set(...)`, stores a continuation placeholder in the trailing slot, and skips that slot during diff rendering. `decode_codepoint(...)` and `next_codepoint_offset(...)` are the UTF-8 stepping helpers for higher-level text rendering libraries. The module is intentionally low-level and is meant to support future TUI libraries on top.
- `std.time.now_ms(...)` lowers to a millisecond clock helper and `std.time.sleep_ms(...)` lowers to a runtime sleep helper.
- `std.math.abs(...)`, `std.math.sign(...)`, `std.math.square(...)`, `std.math.cube(...)`, `std.math.min(...)`, `std.math.max(...)`, `std.math.clamp(...)`, `std.math.between(...)`, `std.math.is_even(...)`, `std.math.is_odd(...)`, `std.math.gcd(...)`, `std.math.lcm(...)`, and `std.math.distance(...)` lower to small integer runtime helpers.
- `std.env.has(...)` and `std.env.get(...)` lower to runtime environment helpers built on libc `getenv`.
- `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, `std.path.is_absolute(...)`, and `std.path.parent(...)` lower to runtime path helpers that understand both `/` and `\\` separators.
- `std.net.is_ipv4(...)` lowers to a dotted-decimal IPv4 validator, `std.net.join_host_port(...)` lowers to a formatting helper that returns an owned `"host:port"` string, the blocking `std.net.tcp_connect(...)`, `std.net.tcp_listen(...)`, `std.net.accept(...)`, `std.net.send(...)`, `std.net.recv(...)`, and `std.net.close(...)` calls lower to embedded IPv4 TCP runtime helpers, and `std.net.udp_bind(...)`, `std.net.udp_send_to(...)`, and `std.net.udp_recv_from(...)` lower to embedded IPv4 UDP runtime helpers.
- Linux and macOS use POSIX/BSD socket calls in the emitted runtime, while Windows lowers through Winsock and links `ws2_32`.
- `std.net.recv(...)` returns an owned runtime string on success, and successful `std.net.udp_recv_from(...)` results contain owned `host` and `data` strings inside `std.net.UdpPacket`. Current socket/listener handles are surfaced to the language as raw `int` values.
- `std.process.platform(...)` and `std.process.arch(...)` return owned copies of target strings, and `std.process.exit(...)` lowers to a runtime exit helper.
- The experimental Linux-only `std.x11.open_window(...)`, `std.x11.pump(...)`, and `std.x11.close(...)` calls lower to embedded X11 runtime helpers. When typed IR uses `std.x11`, the backend auto-links `-lX11`.
- `u8` lowers as LLVM `i8`, `byte` is parsed as an alias for `u8`, matching `u8` comparisons lower with unsigned predicates, and printing `u8` widens through the runtime print helper.
- `slice T` lowers as `{ ptr, i64 }`. Array-to-slice coercion builds that pair from the first element address and the array length, and slice `.length`, indexing, and subslicing lower through aggregate field extraction plus pointer arithmetic.
- `free some_string;` lowers to a tracked string-free helper so owned runtime strings can be released safely without freeing string literals.

## Backend Implementation Notes

- Builtin stdlib validation and lowering currently live in `src/backend/llvm_text.c`.
- Embedded runtime helper emission is split by domain under `src/backend/stdlib/`.
- The new dynamic-storage helpers currently live in `src/backend/stdlib/bytes.c`, `src/backend/stdlib/lines.c`, and `src/backend/stdlib/text.c`.
- The experimental Linux-only X11 runtime helpers currently live in `src/backend/stdlib/x11.c`.

Unsupported lowering reports `E3021` before any LLVM IR text is printed.
