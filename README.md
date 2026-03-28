# cnegative

`cnegative` is a minimal, hackable systems language for learning explicit low-level programming.

It is being shaped as a good place to start before jumping into C, C++, or lower-level systems programming directly. The goal is to keep the language explicit and low-level, while making the surface easier to read, reason about, and modify.

Current project status: `v0.1.0-dev`

This repository currently ships the `cnegc` compiler with:

- structured diagnostics
- lexer, parser, and semantic checking
- typed IR lowering
- LLVM IR text emission
- object emission and binary linking through the host LLVM/Clang toolchain
- benchmark, test, and project-rule checks

## Platform Status

- Language/compiler design target: Linux, macOS, and Windows
- LLVM/toolchain path: cross-platform
- Lexer hot-path asm: x86_64 Linux and x86_64 macOS
- Lexer fallback: portable C on other hosts

## Important Language Rules

- Semicolons are required for import declarations, simple statements, and struct fields.
- Non-void functions must use explicit `return ...;` on every path.
- Conditions must be actual `bool` values.
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
- variables with `let` and `let mut`
- `int`, `bool`, `str`, `void`, `ptr`, and `result`
- `if`, `else`, `while`, `loop`, and range `for`
- structs and public structs
- arrays
- field access and indexing
- module imports and module-qualified public calls
- `alloc`, `addr`, `deref`, `free`, `ok`, `err`, `print`, and `input`
- typed IR lowering
- LLVM IR text emission
- object emission and binary linking
- deep equality across checked aggregate types
- result `.value` guard diagnostics

Current runtime boundary:

- only `input()`-produced strings are tracked as owned runtime strings
- the standard library/runtime surface is still intentionally small

## Build And Run Guide

Full setup, prerequisites, build commands, and user-facing command examples live in [docs/how-to-run-and-build.md](docs/how-to-run-and-build.md).

## Documentation

Main docs:

- [LICENSE](LICENSE)
- [docs/spec.md](docs/spec.md)
- [docs/how-to-run-and-build.md](docs/how-to-run-and-build.md)
- [docs/typed-ir.md](docs/typed-ir.md)
- [docs/llvm-ir.md](docs/llvm-ir.md)
- [docs/project-rules.md](docs/project-rules.md)
- [docs/error-messages.md](docs/error-messages.md)
