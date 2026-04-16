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

Current arithmetic operators on `int` are:

- `+`
- `-`
- `*`
- `/`
- `%`

Literal compatibility today:

- Integer literals still default to `int`.
- Fitting integer literals are accepted where `u8` or `byte` is expected.
- That includes comparisons such as `some_byte == 0`.

Implemented composite type forms:

- `ptr T`
- `result T`
- `slice T`

Current array rule:

- array sizes in types accept non-negative integer constant expressions
- named constants can be used in array sizes, including imported public constants
- `[value; N]` repeats one value across an array literal of size `N`
- prefix type forms bind before array suffixes, so `ptr int[4]` means `array[4] of ptr int`

Current null-pointer rule:

- `null` is a source-level null pointer literal
- `null` can be assigned or passed where a matching `ptr T` is expected
- pointer equality against `null` is supported

Current slice rule:

- `slice T` is a non-owning view over contiguous `T` values.
- Arrays can be used where `slice T` is expected when the element type matches.
- Slices currently expose `.length`, indexing, and subslicing with `[..]`.

### Control Flow

```lang
if x > 5 {
    println(x);
}
```

Strict condition rule:

- A control-flow condition must type-check to `bool`.
- `if x > 5 {}` is valid because `x > 5` is boolean.
- `if x {}` is invalid when `x` is not `bool`.
- No implicit integer truthiness exists.

Narrow `if` expressions are also supported when both branches produce a value:

```lang
let kind:int = if x > 5 { 1 } else { 0 };
```

Current `if` expression rules:

- `else` is required.
- Both branches must produce a non-`void` value.
- Both branches must resolve to the same type.

Current temporary-allocation rule:

- `zone { ... }` creates an explicit temporary allocation scope.
- `zalloc T` is only valid inside an enclosing `zone`.
- zone allocations are released automatically when the `zone` block ends.
- `free` does not apply to zone-owned memory.
- obvious zone escapes through `return` or outer storage are rejected.
- zone-owned values also cannot cross ordinary function call boundaries yet.

### Loops

```lang
while x < 10 {
    x = x + 1;
}
```

```lang
for i:int in 0..10 {
    println(i);
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

Current `main` boundary:

- `main` may return `int`, `u8`, `result int`, `result u8`, or `void`
- `result`-returning `main` allows top-level `try`
- successful `result int` and `result u8` values become the process exit code
- `return err;` from `main` currently maps to process exit code `1`

### Modules

Current module rule:

```lang
import math as m;

fn:int main() {
    return m.add(2, 3);
}
```

`import name;` and dotted imports like `import app.logic;` resolve in this order:

1. builtin `std.*` modules
2. the project root, which is the directory containing the entry file
3. `vendor/` under that project root
4. a legacy fallback relative to the importing file's directory

Project and vendor modules get canonical names from their path relative to those roots, so `feature/helper.cneg` becomes `feature.helper` and `vendor/vendorlib/echo.cneg` becomes `vendorlib.echo`. The loader still accepts legacy `.cn` imports during the transition.

Only `pfn` functions are callable from another module.

The initial standard library is imported through builtin modules using the same surface syntax:

```lang
import std.math as math;
import std.bytes as bytes;
import std.ipc as ipc;
import std.lines as lines;
import std.strings as strings;
import std.text as text;
import std.parse as parse;
import std.fs as fs;
import std.term as term;
import std.net as net;
import std.process as process;
import std.x11 as x11;
```

Current builtin stdlib surface:

- `std.math.abs(int) -> int`
- `std.math.sign(int) -> int`
- `std.math.square(int) -> int`
- `std.math.cube(int) -> int`
- `std.math.is_even(int) -> bool`
- `std.math.is_odd(int) -> bool`
- `std.math.min(int, int) -> int`
- `std.math.max(int, int) -> int`
- `std.math.clamp(int, int, int) -> int`
- `std.math.gcd(int, int) -> int`
- `std.math.lcm(int, int) -> int`
- `std.math.distance(int, int) -> int`
- `std.math.between(int, int, int) -> bool`
- `std.bytes.Buffer { data:ptr u8; length:int; capacity:int }`
- `std.bytes.new() -> result ptr std.bytes.Buffer`
- `std.bytes.with_capacity(int) -> result ptr std.bytes.Buffer`
- `std.bytes.release(ptr std.bytes.Buffer) -> result bool`
- `std.bytes.clear(ptr std.bytes.Buffer) -> result bool`
- `std.bytes.length(ptr std.bytes.Buffer) -> int`
- `std.bytes.capacity(ptr std.bytes.Buffer) -> int`
- `std.bytes.push(ptr std.bytes.Buffer, u8) -> result bool`
- `std.bytes.append(ptr std.bytes.Buffer, slice u8) -> result bool`
- `std.bytes.get(ptr std.bytes.Buffer, int) -> result u8`
- `std.bytes.set(ptr std.bytes.Buffer, int, u8) -> result bool`
- `std.bytes.view(ptr std.bytes.Buffer) -> slice u8`
- `std.ipc.Child { handle:ptr u8 }`
- `std.ipc.spawn(str, slice str) -> result ptr std.ipc.Child`
- `std.ipc.stdin_write(ptr std.ipc.Child, str) -> result int`
- `std.ipc.stdin_write_line(ptr std.ipc.Child, str) -> result int`
- `std.ipc.stdin_close(ptr std.ipc.Child) -> result bool`
- `std.ipc.stdout_read(ptr std.ipc.Child, int) -> result str`
- `std.ipc.stdout_read_line(ptr std.ipc.Child, int) -> result str`
- `std.ipc.request_line(ptr std.ipc.Child, str, int) -> result str`
- `std.ipc.stderr_read(ptr std.ipc.Child, int) -> result str`
- `std.ipc.stderr_read_line(ptr std.ipc.Child, int) -> result str`
- `std.ipc.wait(ptr std.ipc.Child) -> result int`
- `std.ipc.kill(ptr std.ipc.Child) -> result bool`
- `std.ipc.release(ptr std.ipc.Child) -> result bool`
- `std.lines.Buffer { data:ptr str; length:int; capacity:int }`
- `std.lines.new() -> result ptr std.lines.Buffer`
- `std.lines.with_capacity(int) -> result ptr std.lines.Buffer`
- `std.lines.release(ptr std.lines.Buffer) -> result bool`
- `std.lines.clear(ptr std.lines.Buffer) -> result bool`
- `std.lines.length(ptr std.lines.Buffer) -> int`
- `std.lines.capacity(ptr std.lines.Buffer) -> int`
- `std.lines.get(ptr std.lines.Buffer, int) -> result str`
- `std.lines.set(ptr std.lines.Buffer, int, str) -> result bool`
- `std.lines.push(ptr std.lines.Buffer, str) -> result bool`
- `std.lines.insert(ptr std.lines.Buffer, int, str) -> result bool`
- `std.lines.remove(ptr std.lines.Buffer, int) -> result bool`
- `std.strings.len(str) -> int`
- `std.strings.copy(str) -> str`
- `std.strings.concat(str, str) -> str`
- `std.strings.from_int(int) -> str`
- `std.strings.eq(str, str) -> bool`
- `std.strings.starts_with(str, str) -> bool`
- `std.strings.ends_with(str, str) -> bool`
- `std.text.Builder { data:ptr u8; length:int; capacity:int }`
- `std.text.new() -> result ptr std.text.Builder`
- `std.text.with_capacity(int) -> result ptr std.text.Builder`
- `std.text.release(ptr std.text.Builder) -> result bool`
- `std.text.clear(ptr std.text.Builder) -> result bool`
- `std.text.length(ptr std.text.Builder) -> int`
- `std.text.capacity(ptr std.text.Builder) -> int`
- `std.text.append(ptr std.text.Builder, str) -> result bool`
- `std.text.append_int(ptr std.text.Builder, int) -> result bool`
- `std.text.push_byte(ptr std.text.Builder, u8) -> result bool`
- `std.text.build(ptr std.text.Builder) -> result str`
- `std.text.view(ptr std.text.Builder) -> slice u8`
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
- `std.term.Cell { code:int; fg:int; bg:int; attrs:int; wide:bool }`
- `std.term.Buffer { width:int; height:int; cells:ptr std.term.Cell }`
- `std.term.Clip { row:int; column:int; height:int; width:int }`
- `std.term.Event { kind:int; code:int; modifiers:int; row:int; column:int }`
- `std.term.EVENT_KEY:int`
- `std.term.EVENT_MOUSE:int`
- `std.term.EVENT_RESIZE:int`
- `std.term.EVENT_PASTE:int`
- `std.term.COLOR_DEFAULT:int`
- `std.term.COLOR_BLACK:int` through `std.term.COLOR_WHITE:int`
- `std.term.COLOR_BRIGHT_BLACK:int` through `std.term.COLOR_BRIGHT_WHITE:int`
- `std.term.ATTR_NONE:int`
- `std.term.ATTR_BOLD:int`
- `std.term.ATTR_DIM:int`
- `std.term.ATTR_ITALIC:int`
- `std.term.ATTR_UNDERLINE:int`
- `std.term.ATTR_BLINK:int`
- `std.term.ATTR_REVERSE:int`
- `std.term.ATTR_STRIKETHROUGH:int`
- `std.term.MOD_SHIFT:int`
- `std.term.MOD_ALT:int`
- `std.term.MOD_CTRL:int`
- `std.term.MOUSE_LEFT:int`
- `std.term.MOUSE_MIDDLE:int`
- `std.term.MOUSE_RIGHT:int`
- `std.term.MOUSE_RELEASE:int`
- `std.term.MOUSE_MOVE:int`
- `std.term.MOUSE_SCROLL_UP:int`
- `std.term.MOUSE_SCROLL_DOWN:int`
- `std.term.KEY_ESCAPE:int`
- `std.term.KEY_ENTER:int`
- `std.term.KEY_TAB:int`
- `std.term.KEY_BACKSPACE:int`
- `std.term.KEY_UP:int`
- `std.term.KEY_DOWN:int`
- `std.term.KEY_LEFT:int`
- `std.term.KEY_RIGHT:int`
- `std.term.KEY_HOME:int`
- `std.term.KEY_END:int`
- `std.term.KEY_PAGE_UP:int`
- `std.term.KEY_PAGE_DOWN:int`
- `std.term.KEY_INSERT:int`
- `std.term.KEY_DELETE:int`
- `std.term.KEY_F1:int` through `std.term.KEY_F12:int`
- `std.term.is_tty() -> bool`
- `std.term.columns() -> result int`
- `std.term.rows() -> result int`
- `std.term.term_name() -> result str`
- `std.term.supports_truecolor() -> bool`
- `std.term.supports_256color() -> bool`
- `std.term.supports_unicode() -> bool`
- `std.term.supports_mouse() -> bool`
- `std.term.read_byte() -> result int`
- `std.term.read_byte_timeout(int) -> result int`
- `std.term.read_event() -> result std.term.Event`
- `std.term.read_event_timeout(int) -> result std.term.Event`
- `std.term.read_paste() -> result str`
- `std.term.rgb(int, int, int) -> int`
- `std.term.codepoint_width(int) -> int`
- `std.term.string_width(str) -> int`
- `std.term.decode_codepoint(str, int) -> result int`
- `std.term.next_codepoint_offset(str, int) -> result int`
- `std.term.set_style(int, int, int) -> void`
- `std.term.reset_style() -> void`
- `std.term.enable_mouse() -> void`
- `std.term.disable_mouse() -> void`
- `std.term.enable_bracketed_paste() -> void`
- `std.term.disable_bracketed_paste() -> void`
- `std.term.buffer_new(int, int) -> result ptr std.term.Buffer`
- `std.term.buffer_resize(ptr std.term.Buffer, int, int, std.term.Cell) -> result bool`
- `std.term.buffer_free(ptr std.term.Buffer) -> result bool`
- `std.term.buffer_clear(ptr std.term.Buffer, std.term.Cell) -> result bool`
- `std.term.buffer_set(ptr std.term.Buffer, int, int, std.term.Cell) -> result bool`
- `std.term.buffer_get(ptr std.term.Buffer, int, int) -> result std.term.Cell`
- `std.term.render_diff(ptr std.term.Buffer, ptr std.term.Buffer) -> result bool`
- `std.term.render_diff_clip(ptr std.term.Buffer, ptr std.term.Buffer, std.term.Clip) -> result bool`
- `std.term.write(str) -> void`
- `std.term.flush() -> void`
- `std.term.clear() -> void`
- `std.term.clear_line() -> void`
- `std.term.clear_line_left() -> void`
- `std.term.clear_line_right() -> void`
- `std.term.move_cursor(int, int) -> void`
- `std.term.save_cursor() -> void`
- `std.term.restore_cursor() -> void`
- `std.term.hide_cursor() -> void`
- `std.term.show_cursor() -> void`
- `std.term.enter_alt_screen() -> void`
- `std.term.leave_alt_screen() -> void`
- `std.term.set_scroll_region(int, int) -> void`
- `std.term.reset_scroll_region() -> void`
- `std.term.enter_raw_mode() -> result bool`
- `std.term.leave_raw_mode() -> result bool`
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
- Linux only: `std.x11.open_window(str, int, int) -> result int`
- Linux only: `std.x11.pump(int) -> result bool`
- Linux only: `std.x11.close(int) -> result bool`

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
- `if r.ok == false { return err; }` is also a valid proof pattern.
- immutable bool aliases like `let ready:bool = r.ok; if ready { ... }` are also understood.
- simple composed guards like `if r.ok && cond { ... }` and `if blocked || cond { return err; }` are understood when they prove the result is ok on the active branch.
- `let first:int = if r.ok { r.value[0] } else { 0 };` is valid.
- `while r.ok { return r.value[0]; }` is also a valid proof pattern.
- `return r.value;` without such a proof is rejected.

Contextual keyword rule:

- `result` is treated as the `result T` type constructor only in type positions.
- Outside type parsing, `result` can be used as a normal identifier or import alias.

Current output rule:

- `print(...)` writes without an implicit newline.
- `println(...)` writes the value and appends a newline.

`try` is the shortcut for the common unwrap-or-return path inside `result ...` functions:

```lang
fn:result int plus_one(value:int) {
    try inner = divide(value, 2);
    return ok (inner + 1);
}
```

Current checked rule for `try`:

- the enclosing function must return `result ...`
- the initializer must also have type `result ...`
- on success, the binding after `try` has the unwrapped inner type
- on failure, `try` returns `err` from the current function

`defer` runs cleanup when the current scope exits:

```lang
fn:result int load() {
    let heap:ptr int = alloc int;
    defer free heap;
    return ok 1;
}
```

Current `defer` surface is intentionally small:

- `defer expr;`
- `defer free value;`

`zone` is the scoped temporary-allocation form:

```lang
fn:int main() {
    let mut total:int = 0;

    zone {
        let value:ptr int = zalloc int;
        deref value = 7;
        total = deref value;
    }

    return total;
}
```

Current `zone` rule:

- `alloc` still means normal heap allocation and still needs `free`
- `zalloc` means temporary zone allocation
- zone allocations are released when the block ends
- `free` on zone-owned values is rejected
- obvious escapes like `return value;` or assigning into outer storage are rejected
- passing zone-owned values to ordinary function parameters is rejected for now
- this feature is intentionally explicit and does not change manual string ownership

String literals now also support raw multiline backtick form:

```lang
let banner:str = `cnegative
rocks`;
```

Backtick strings keep the inner text as-is and can span lines.

### Arrays and Slices

Arrays own a fixed-size block of values:

```lang
let data:int[4] = [3, 4, 5, 6];
```

Slices are lightweight views over that contiguous data:

```lang
fn:int sum(xs:slice int) {
    return xs[0] + xs[1] + xs.length;
}

fn:int main() {
    let data:int[4] = [3, 4, 5, 6];
    let view:slice int = data;
    return sum(view);
}
```

Current slice rules:

- `slice T` does not own memory by itself.
- A matching array can be assigned to a `slice T` binding.
- A matching array can be passed directly to a function that expects `slice T`.
- Slice field access is currently limited to `.length`.
- Indexing works on both arrays and slices.
- Subslicing works on both arrays and slices with forms such as `xs[1..4]`, `xs[..4]`, and `xs[1..]`.

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

`build/cnegc llvm-ir <file>` lowers the checked subset into textual LLVM IR. The current backend handles `int`, `bool`, `str`, arrays, slices, structs, `ptr`, `result`, structured control flow, local bindings, allocation/free, local calls, and imported module calls. `build/cnegc obj <file> [output]` emits an object file, and `build/cnegc build <file> [output]` links a runnable binary through `clang-18` or `clang`.

Current runtime notes:

- `input()` returns an owned heap-backed string copy in the generated runtime helper.
- `std.io.read_line()` lowers to the same owned heap-backed runtime helper as `input()`.
- `std.term` is the first low-level terminal/TUI slice. `write(...)`, `flush()`, `clear()`, line erase helpers, `move_cursor(...)`, save/restore cursor, cursor visibility, alternate-screen toggles, scroll-region helpers, `is_tty()`, terminal size queries, terminal capability queries, raw-mode enter/leave, raw and timed byte/event reads, style/color helpers, width helpers, buffer resize, and screen-buffer diff rendering all lower to embedded terminal helpers. Coordinates in `move_cursor(row, column)` and `set_scroll_region(top, bottom)` are zero-based at the language level and become one-based VT cursor positions in the runtime.
- `std.bytes` is the first dynamic byte-storage slice built on top of core `slice T`. `Buffer` is a growable heap-owned byte container. `new()`, `with_capacity(...)`, `release(...)`, `clear(...)`, `length(...)`, `capacity(...)`, `push(...)`, `append(...)`, `get(...)`, `set(...)`, and `view(...)` lower to embedded runtime helpers. `view(...)` returns a non-owning `slice u8` over the current buffer contents.
- `std.ipc` is the first cross-language child-process IPC slice. `spawn(program, args)` launches a child without shell string hardcoding, `stdin_write(...)` / `stdin_write_line(...)` write text to the child's stdin pipe, `stdout_read(...)` / `stderr_read(...)` expose raw chunk reads, `stdout_read_line(...)` / `stderr_read_line(...)` expose newline-delimited protocol reads, `request_line(...)` covers the common one-request/one-response stdout pattern, and `wait(...)`, `kill(...)`, and `release(...)` manage the child lifecycle. This first pass is blocking and text-first by design.
- `std.lines` is the first line-oriented dynamic-storage slice for editor-style code. `Buffer` owns copies of inserted lines, `get(...)` returns a borrowed `str` view into that storage, and `set(...)`, `push(...)`, `insert(...)`, and `remove(...)` lower to embedded runtime helpers for line duplication and shifting.
- `std.text` is the first dynamic text-building slice built on top of `std.bytes`. `Builder` uses the same growable storage shape, but `append(...)` accepts `str`, `push_byte(...)` appends one byte, `view(...)` returns a `slice u8`, and `build(...)` returns a freshly owned `str` on success.
- `std.term.term_name()` returns an owned runtime string when the terminal name can be discovered.
- `std.term.supports_truecolor()`, `std.term.supports_256color()`, `std.term.supports_unicode()`, and `std.term.supports_mouse()` lower to terminal-capability helpers built on environment/TTY checks.
- `std.term.read_byte()` returns one raw input byte as `result int`, and `std.term.read_byte_timeout(timeout_ms)` does the same with a timeout.
- `std.term.read_event()` returns `result std.term.Event`, and `std.term.read_event_timeout(timeout_ms)` adds a timeout path for polling loops. The current slice can emit `EVENT_KEY`, `EVENT_MOUSE`, `EVENT_RESIZE`, and `EVENT_PASTE`. Keyboard events use printable codepoints or `KEY_*` constants. Mouse events use `MOUSE_*` constants. Resize events report the new terminal size through `event.row` and `event.column`. On Unix, resize polling is now signal-backed through `SIGWINCH` before the runtime re-queries rows and columns. Paste events signal that a bracketed paste started, and `std.term.read_paste()` returns the pasted bytes as an owned string.
- `std.term.enable_mouse()` / `disable_mouse()` and `std.term.enable_bracketed_paste()` / `disable_bracketed_paste()` lower to terminal escape helpers that turn those input modes on and off.
- `std.term.rgb(r, g, b)` packs a truecolor value for `set_style(...)`. `std.term.set_style(fg, bg, attrs)` and `std.term.reset_style()` lower to ANSI SGR helpers. The current color surface supports `COLOR_DEFAULT`, the 16-color palette, 256-color integers, and RGB values produced by `rgb(...)`.
- `std.term.codepoint_width(...)` and `std.term.string_width(...)` lower to runtime width helpers for terminal layout math.
- `std.term.decode_codepoint(text, offset)` and `std.term.next_codepoint_offset(text, offset)` expose UTF-8 stepping helpers so higher-level `.cneg` libraries can walk terminal text without dropping into backend C code.
- `slice T` currently lowers as a simple `{ ptr, i64 }` pair in LLVM-facing codegen. Array-to-slice coercion materializes a pointer to the first element and the array length.
- `std.term.Cell` is the basic render unit: codepoint, foreground, background, attributes, and a `wide` flag. `buffer_set(...)` now also auto-detects wide codepoints, writes the normalized cell, and stores an internal continuation placeholder in the trailing slot so the diff renderer does not paint that slot separately.
- `std.term.buffer_new(width, height)` allocates a runtime-owned screen buffer. `buffer_resize(...)`, `buffer_clear(...)`, `buffer_set(...)`, and `buffer_get(...)` operate on zero-based row/column coordinates and return `result` so invalid sizes or out-of-bounds access fail cleanly.
- `std.term.render_diff(back, front)` compares the desired back buffer against the already-rendered front buffer, emits only changed cells, reuses the current cursor position across adjacent changed cells, copies the rendered cells into `front`, resets the style, and flushes output.
- `std.term.render_diff_clip(back, front, clip)` does the same work, but only inside the `std.term.Clip` region.
- `std.term` currently focuses on ANSI-capable terminals and direct terminal control rather than widgets or layouts. It is the foundation layer for future TUI work, not the final TUI framework.
- `std.time.now_ms()` lowers to a runtime clock helper that returns the current wall-clock time in milliseconds.
- `std.time.sleep_ms(int)` lowers to a runtime sleep helper.
- `std.math` currently lowers to integer-only runtime helpers for sign/absolute-value helpers, parity checks, min/max/clamp/between helpers, and `gcd`/`lcm`/distance-style helpers.
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
- `std.x11` is currently a tiny experimental Linux-only window module for stress testing real host integration. `open_window(title, width, height)` returns a raw window handle as `result int`, `pump(handle)` reports whether the window should keep running, and `close(handle)` destroys the native window.
- `addr value` requires real mutable storage. Immutable `let` bindings fail with `E3035`, and module constants fail with `E3036`.
- `free some_string;` releases tracked owned strings created by `input()`, `std.io.read_line()`, `str_copy(...)`, `str_concat(...)`, `std.strings.copy(...)`, `std.strings.concat(...)`, successful `std.text.build(...)`, `std.term.read_paste(...)`, `std.term.term_name(...)`, `std.fs.read_text(...)`, `std.fs.cwd(...)`, `std.env.get(...)`, `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, `std.path.parent(...)`, `std.net.join_host_port(...)`, `std.net.recv(...)`, the `host` and `data` fields from successful `std.net.udp_recv_from(...)`, successful `std.ipc.stdout_read(...)`, successful `std.ipc.stdout_read_line(...)`, successful `std.ipc.request_line(...)`, successful `std.ipc.stderr_read(...)`, successful `std.ipc.stderr_read_line(...)`, `std.process.platform(...)`, or `std.process.arch(...)`. Freeing string literals is a safe no-op in the generated runtime.
- `free some_slice;` is rejected with `E3037` because slices are non-owning views. `free some_result;` is rejected with `E3038`; unwrap the owned payload first.
- The tracked allocator now reports dedicated runtime memory codes for allocation failure, size overflow in counted allocation/reallocation, unmanaged `realloc`/`free`, double free, interior-pointer `free`, best-effort quarantine-window use-after-free, guard-detected overflow/underflow, and leak reporting. See `R4001`, `R4002`, `R4003`, `R4004`, `R4005`, `R4006`, `R4008`, `R4009`, `R4010`, `R4011`, `R4013`, `R4016`, and `R4017`.
- `str` equality in the backend is content-based through `strcmp`, not pointer-identity based.
- The parser now recovers across common statement and top-level syntax errors so one missing `;` does not collapse the whole file into a single follow-on failure.
