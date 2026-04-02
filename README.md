# Development may progress slowly as I am currently focused on my exams.
# cnegative

`cnegative` is a minimal, hackable systems language for learning explicit low-level programming.

It is being shaped as a good place to start before jumping into C, C++, or lower-level systems programming directly. The goal is to keep the language explicit and low-level, while making the surface easier to read, reason about, and modify.

Current project status: `v0.4.0`

This repository currently ships the `cnegc` compiler with:

- structured diagnostics
- lexer, parser, and semantic checking
- typed IR lowering
- typed IR optimization
- LLVM IR text emission
- object emission and binary linking through the host LLVM/Clang toolchain
- benchmark, test, and project-rule checks
- optional Python-based blocking TCP and UDP integration tests

## Platform Status

- Language/compiler design target: Linux, macOS, and Windows
- LLVM/toolchain path: cross-platform
- Lexer hot-path asm: x86_64 Linux and x86_64 macOS
- Lexer fallback: portable C on other hosts

## Important Language Rules

- Semicolons are required for import declarations, simple statements, and struct fields.
- Non-void functions must use explicit `return ...;` on every path.
- Conditions must be actual `bool` values.
- Module-level constants use `const` and `pconst`.
- The language is explicit by design. `if x {}` is rejected when `x` is not `bool`.

Valid:

```lang
let x:int = 7;
if x > 5 {
    print(x);
}
```

Rejected:

```lang
let x:int = 7;
if x {
    print(x);
}
```

## Quick Start

If you already have `cnegc`:

```sh
cnegc check examples/valid_basic.cneg
cnegc build examples/valid_basic.cneg build/valid_basic
```

`check`, `ir`, and `llvm-ir` need only `cnegc`.

`obj` and `build` need `clang-18` or `clang` in `PATH`.

## Current Compiler Surface

Implemented today:

- functions and public functions
- module-level constants with `const` and `pconst`
- variables with `let` and `let mut`
- `int`, `u8`, `bool`, `str`, `void`, `ptr`, `result`, and `slice`
- `byte` as a readable alias for `u8`
- `slice T` as a non-owning view over contiguous values
- `if`, `else`, `while`, `loop`, and range `for`
- narrow value-producing `if` expressions in the form `if cond { expr } else { expr }`
- `defer expr;` and `defer free value;` for scope-exit cleanup
- `try name = some_result();` inside `result ...` functions for unwrap-or-return flow
- structs and public structs
- arrays
- `slice T` views with `.length`, indexing, subslicing, and array-to-slice coercion
- field access and indexing
- module imports and module-qualified public calls
- initial stdlib modules: `std.math`, `std.bytes`, `std.strings`, `std.text`, `std.parse`, `std.fs`, `std.io`, `std.term`, `std.time`, `std.env`, `std.path`, `std.net`, `std.process`, and the experimental Linux-only `std.x11`
- `std.bytes.Buffer` as a growable byte container with append, get/set, and slice-view helpers
- `std.text.Builder` as a growable text builder that can accumulate strings and return a final owned `str`
- a low-level terminal foundation through `std.term`, including capability queries, raw and timed byte/event reads, key/mouse/resize/paste events, cursor/scroll control, style/color control including RGB helpers, width helpers, buffer resize, and clipped diff rendering
- beginner-first blocking IPv4 TCP and UDP helpers in `std.net`
- a tiny real-window stress-test path through the experimental Linux-only `std.x11`
- `alloc`, `addr`, `deref`, `free`, `ok`, `err`, `print`, and `input`
- `str_copy` and `str_concat`
- typed IR lowering
- typed IR optimization and constant folding
- LLVM IR text emission
- object emission and binary linking
- deep equality across checked aggregate types
- result `.value` proof diagnostics
- stronger result narrowing after checks like `if r.ok == false { return err; }`
- raw backtick strings for multiline text without escape processing
- parser recovery for continued syntax diagnostics after one error

Current integer rule:

- `int` arithmetic operators currently include `+`, `-`, `*`, `/`, and `%`
- `u8` is a real primitive byte-sized value type
- `byte` is an alias for `u8`
- integer literals fit into `u8` automatically when a `u8` is expected
- fitting integer literals also compare cleanly against `u8` and `byte` values
- arithmetic stays `int`-only for now
- equality and ordered comparisons work for matching `u8` values

`std.math` is also integer-only today and includes:

- `abs`, `sign`, `square`, `cube`
- `min`, `max`, `clamp`, `between`
- `is_even`, `is_odd`
- `gcd`, `lcm`, `distance`

Current runtime boundary:

- owned runtime strings are currently produced by `input()`, `std.io.read_line(...)`, `str_copy(...)`, `str_concat(...)`, `std.strings.copy(...)`, `std.strings.concat(...)`, successful `std.text.build(...)`, `std.term.read_paste(...)`, `std.term.term_name(...)`, `std.fs.read_text(...)`, `std.fs.cwd(...)`, `std.env.get(...)`, `std.path.join(...)`, `std.path.file_name(...)`, `std.path.stem(...)`, `std.path.extension(...)`, `std.path.parent(...)`, `std.net.join_host_port(...)`, `std.net.recv(...)`, the `host` and `data` fields from successful `std.net.udp_recv_from(...)`, `std.process.platform(...)`, and `std.process.arch(...)`
- `free some_string;` safely releases tracked owned strings from those producers
- the standard library/runtime surface is still intentionally small

## Build And Run Guide

Full setup, prerequisites, build commands, and user-facing command examples live in [docs/how-to-run-and-build.md](docs/how-to-run-and-build.md).

## Install

Prebuilt compiler install steps live in [install.md](install.md).

## Documentation

Main docs:

- [LICENSE](LICENSE)
- [install.md](install.md)
- [docs/spec.md](docs/spec.md)
- [docs/how-to-run-and-build.md](docs/how-to-run-and-build.md)
- [docs/typed-ir.md](docs/typed-ir.md)
- [docs/llvm-ir.md](docs/llvm-ir.md)
- [docs/project-rules.md](docs/project-rules.md)
- [docs/error-messages.md](docs/error-messages.md)
