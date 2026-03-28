# How To Run And Build

This document covers two different workflows:

- building the `cnegc` compiler from this repository
- using an existing `cnegc` binary to check or build `.cneg` programs

## User Tool Requirements

There is no interpreter yet. A `.cneg` file does not run directly by itself.

If a user already has a `cnegc` binary:

- `cnegc check <file>`
- `cnegc ir <file>`
- `cnegc llvm-ir <file>`

These commands require only `cnegc`.

To produce native output:

- `cnegc obj <file> [output]`
- `cnegc build <file> [output]`

These commands require:

- `cnegc`
- `clang-18` or `clang` in `PATH`

After `cnegc build` produces a native executable, running that executable does not require `clang`.

Practical summary:

- checking source: `cnegc`
- dumping typed IR: `cnegc`
- dumping LLVM IR: `cnegc`
- emitting objects: `cnegc` + `clang`
- building native executables: `cnegc` + `clang`
- running built executables: only the built executable

## Build `cnegc` From Source

### Prerequisites

To build the compiler from this repository, you need:

- a C compiler available as `cc` for the `Makefile` path, or a normal C compiler for CMake
- either `make` or CMake 3.20+

To run the full test suite, you also need:

- `clang-18` or `clang` in `PATH`
- `bash` if using `make test`

`llvm-as-18` or `llvm-as` is optional. The smoke tests use it when available and otherwise fall back to `clang -c -x ir`.

### Build With `make`

```sh
make
```

This produces:

```text
build/cnegc
```

Run the tests:

```sh
make test
```

### Build With CMake

```sh
cmake -S . -B out
cmake --build out
```

This produces:

```text
out/build/cnegc
```

Run the tests:

```sh
ctest --test-dir out --output-on-failure
```

## Use `cnegc`

Examples below assume `cnegc` is either in `PATH` or called by full path such as `./build/cnegc`.

### Check A Source File

```sh
cnegc check examples/valid_basic.cneg
```

### Dump Typed IR

```sh
cnegc ir examples/valid_basic.cneg
```

### Dump LLVM IR

```sh
cnegc llvm-ir examples/valid_basic.cneg
```

### Emit An Object File

Requires `clang-18` or `clang` in `PATH`.

```sh
cnegc obj examples/valid_basic.cneg build/valid_basic.o
```

### Build A Runnable Program

Requires `clang-18` or `clang` in `PATH`.

```sh
cnegc build examples/valid_basic.cneg build/valid_basic
```

On Windows, the output is typically:

```text
build/valid_basic.exe
```

### Run The Lexer Benchmark

```sh
cnegc bench-lexer examples/valid_basic.cneg 500
```
