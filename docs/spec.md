# cnegative Language Spec

## Core Position

`cnegative` is explicit, readable, and low-level. It keeps manual control, reduces hidden behavior, and prefers words over symbolic shortcuts when that improves clarity.

## Current Syntax Direction

### Functions

```lang
fn:int main() {
    return 0;
}
```

Public functions use `pfn`.

```lang
pfn:int add(a:int, b:int) {
    return a + b;
}
```

Public structs use `pstruct`.

```lang
pstruct Point {
    x:int;
    y:int;
}
```

Module-level constants use `const` and `pconst`.

```lang
const LIMIT:int = 16;
pconst GREETING:str = "hello";
```

### Variables

```lang
let x:int = 10;
let mut y:int = 20;
```

Statement rule:

- Simple statements end with `;`.
- Struct fields end with `;`.
- Block forms such as `if`, `while`, `for`, `loop`, `fn`, and `struct` do not take a trailing `;` after the closing `}`.

### Primitive Types

- `int`
- `u8`
- `bool`
- `str`
- `void`

`byte` is a source-level alias for `u8`.

Implemented composite type forms:

- `ptr T`
- `result T`

### Control Flow

```lang
if x > 5 {
    print(x);
}
```

Strict condition rule:

- A control-flow condition must type-check to `bool`.
- `if x > 5 {}` is valid because `x > 5` is boolean.
- `if x {}` is invalid when `x` is not `bool`.
- No implicit integer truthiness exists.

### Loops

```lang
while x < 10 {
    x = x + 1;
}
```

```lang
for i:int in 0..10 {
    print(i);
}
```

```lang
loop {
}
```

### Returns

Explicit return is required in non-void functions:

```lang
fn:int add(a:int, b:int) {
    return a + b;
}
```

### Modules

Current local module rule:

```lang
import math as m;

fn:int main() {
    return m.add(2, 3);
}
```

`import name;` resolves `name.cneg` relative to the importing file. Only `pfn` functions are callable from another module. The loader still accepts legacy `.cn` imports during the transition.

The initial standard library is imported through builtin modules using the same surface syntax:

```lang
import std.math as math;
import std.strings as strings;
import std.parse as parse;
import std.fs as fs;
import std.net as net;
import std.process as process;
```

Current builtin stdlib surface:

- `std.math.abs(int) -> int`
- `std.math.min(int, int) -> int`
- `std.math.max(int, int) -> int`
- `std.math.clamp(int, int, int) -> int`
- `std.strings.len(str) -> int`
- `std.strings.copy(str) -> str`
- `std.strings.concat(str, str) -> str`
- `std.strings.eq(str, str) -> bool`
- `std.strings.starts_with(str, str) -> bool`
- `std.strings.ends_with(str, str) -> bool`
- `std.parse.to_int(str) -> result int`
- `std.parse.to_bool(str) -> result bool`
- `std.fs.exists(str) -> bool`
- `std.fs.cwd() -> result str`
- `std.fs.create_dir(str) -> result bool`
- `std.fs.remove_dir(str) -> result bool`
- `std.fs.read_text(str) -> result str`
- `std.fs.file_size(str) -> result int`
- `std.fs.copy(str, str) -> result bool`
- `std.fs.write_text(str, str) -> result bool`
- `std.fs.append_text(str, str) -> result bool`
- `std.fs.rename(str, str) -> result bool`
- `std.fs.move(str, str) -> result bool`
- `std.fs.remove(str) -> result bool`
- `std.io.write(str) -> void`
- `std.io.write_line(str) -> void`
- `std.io.read_line() -> str`
- `std.time.now_ms() -> int`
- `std.time.sleep_ms(int) -> void`
- `std.env.has(str) -> bool`
- `std.env.get(str) -> result str`
- `std.path.join(str, str) -> str`
- `std.path.file_name(str) -> str`
- `std.path.stem(str) -> str`
- `std.path.extension(str) -> str`
- `std.path.is_absolute(str) -> bool`
- `std.path.parent(str) -> result str`
- `std.net.is_ipv4(str) -> bool`
- `std.net.join_host_port(str, int) -> str`
- `std.net.tcp_connect(str, int) -> result int`
- `std.net.tcp_listen(str, int) -> result int`
- `std.net.accept(int) -> result int`
- `std.net.send(int, str) -> result int`
- `std.net.recv(int, int) -> result str`
- `std.net.UdpPacket { host:str; port:int; data:str }`
- `std.net.udp_bind(str, int) -> result int`
- `std.net.udp_send_to(int, str, int, str) -> result int`
- `std.net.udp_recv_from(int, int) -> result std.net.UdpPacket`
- `std.net.close(int) -> result bool`
- `std.process.platform() -> str`
- `std.process.arch() -> str`
- `std.process.exit(int) -> void`

Public constants are also available through a qualified module name:

```lang
import numbers as n;

const LOCAL:int = n.BASE + 4;
```

Current constant rules:

- `pconst` exports a module-level constant.
- Constant initializers must stay compile-time and cannot perform runtime calls or memory operations.
- Cyclic constant definitions are rejected.

Imported struct types are also available through a qualified name:

```lang
import shapes as s;

fn:int main() {
    let p:s.Point = s.Point {
        x:1,
        y:2
    };

    return p.x;
}
```

The module must still be imported first. `shapes.Point` without a matching `import shapes;` is rejected. Cross-module struct usage only works for `pstruct` declarations.

### Pointer and Result Forms

```lang
let mut x:int = 10;
let p:ptr int = addr x;
p.value = 11;
deref p = 12;

let heap:ptr int = alloc int;
heap.value = x;
deref heap = x;
free heap;
```

```lang
fn:result int divide(a:int, b:int) {
    if b == 0 {
        return err;
    }

    return ok a / b;
}
```

Current checked rule for `result` access:

- `.ok` is always readable.
- `.value` requires proof in the current scope that the named result is ok.
- `if r.ok { return r.value; }` is valid.
- `return r.value;` without such a guard is rejected.

## Current Pipeline

```text
Source
-> Lexer
-> Parser
-> AST
-> Semantic Analysis
-> Typed IR
-> Typed IR Optimization
-> LLVM IR
-> Object File
-> Static Linking
-> Binary
```

`build/cnegc ir <file>` dumps the checked project as typed IR after constant folding and simple dead-statement cleanup. The current IR keeps structured control flow, preserves explicit returns, and resolves module-qualified calls and constants to canonical module names.

`build/cnegc llvm-ir <file>` lowers the checked subset into textual LLVM IR. The current backend handles `int`, `bool`, `str`, arrays, structs, `ptr`, `result`, structured control flow, local bindings, allocation/free, local calls, and imported module calls. `build/cnegc obj <file> [output]` emits an object file, and `build/cnegc build <file> [output]` links a runnable binary through `clang-18` or `clang`.

Current runtime notes:

- `input()` returns an owned heap-backed string copy in the generated runtime helper.
- `std.io.read_line()` lowers to the same owned heap-backed runtime helper as `input()`.
- `std.time.now_ms()` lowers to a runtime clock helper that returns the current wall-clock time in milliseconds.
- `std.time.sleep_ms(int)` lowers to a runtime sleep helper.
- `std.math` currently lowers to integer-only runtime helpers for absolute value, min/max, and clamp.
- `str_copy(s)` returns a new owned heap-backed copy of `s`.
- `str_concat(a, b)` returns a new owned heap-backed concatenated string.
- `std.strings.copy(s)` and `std.strings.concat(a, b)` lower to the same owned-string runtime helpers as `str_copy(...)` and `str_concat(...)`.
- `std.fs.read_text(path)` returns an owned heap-backed string on success.
- `std.fs.cwd()` returns an owned heap-backed string on success.
- `std.fs.file_size(path)` returns the current byte count of the file contents on success.
- `std.fs.copy(from, to)` copies file bytes into a new destination file.
- `std.fs.move(from, to)` is the clearer stdlib wrapper over file rename/move behavior.
- `std.env.get(name)` returns an owned heap-backed string copy of the environment value on success.
- `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, and successful `std.path.parent(...)` calls return owned heap-backed strings.
- `std.path.is_absolute(path)` checks leading separators and Windows-style drive roots.
- `std.net.is_ipv4(value)` checks a dotted-decimal IPv4 literal such as `"127.0.0.1"`.
- `std.net.join_host_port(host, port)` returns an owned heap-backed `"host:port"` string.
- `std.net.tcp_connect(host, port)` opens a blocking IPv4 TCP client connection and returns a raw socket handle as `result int`.
- `std.net.tcp_listen(host, port)` opens a blocking IPv4 TCP listener and returns a raw listener handle as `result int`.
- `std.net.accept(listener)` blocks until one client connects and returns the accepted socket handle as `result int`.
- `std.net.send(socket, value)` blocks until the whole string is written or the send fails.
- `std.net.recv(socket, max_bytes)` blocks until a chunk arrives or the peer closes, and returns an owned heap-backed string on success.
- `std.net.udp_bind(host, port)` opens a blocking IPv4 UDP socket bound to the given local address and returns a raw socket handle as `result int`.
- `std.net.udp_send_to(socket, host, port, value)` sends the full string as one UDP datagram.
- `std.net.udp_recv_from(socket, max_bytes)` blocks until one UDP datagram arrives and returns `result std.net.UdpPacket`, where `packet.host` and `packet.data` are owned heap-backed strings and `packet.port` is the sender port.
- `std.net.close(handle)` closes a socket/listener handle and returns `result bool`.
- `std.net` currently targets blocking IPv4 TCP and UDP only. Linux and macOS lower through POSIX/BSD socket calls, and Windows lowers through Winsock.
- `std.process.platform()` and `std.process.arch()` return owned heap-backed copies of the current target platform/architecture strings.
- `std.process.exit(code)` lowers to a runtime process-exit helper.
- `free some_string;` releases tracked owned strings created by `input()`, `std.io.read_line()`, `str_copy(...)`, `str_concat(...)`, `std.strings.copy(...)`, `std.strings.concat(...)`, `std.fs.read_text(...)`, `std.fs.cwd(...)`, `std.env.get(...)`, `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, `std.path.parent(...)`, `std.net.join_host_port(...)`, `std.net.recv(...)`, the `host` and `data` fields from successful `std.net.udp_recv_from(...)`, `std.process.platform(...)`, or `std.process.arch(...)`. Freeing string literals is a safe no-op in the generated runtime.
- `str` equality in the backend is content-based through `strcmp`, not pointer-identity based.
- The parser now recovers across common statement and top-level syntax errors so one missing `;` does not collapse the whole file into a single follow-on failure.
