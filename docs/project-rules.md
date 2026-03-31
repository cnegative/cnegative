# Project Rules

## Implementation Rules

1. Compiler and tooling code is written in C.
2. Performance-critical hot paths are reserved for assembly once profiling proves they matter.
3. No source file should exceed 5000 lines.
4. Developer-facing memory leak tracking must be enabled from the beginning.
5. Diagnostics must be specific, stable, and documented.
6. Statement-terminator rules stay explicit: semicolons are required for simple statements.

## Enforced Checks

- `make check-lines` rejects files over the line cap.
- The compiler uses a tracked allocator and prints leaks on shutdown if memory is left live.
- Error codes are documented in `docs/error-messages.md`.

## Layout

- `include/cnegative/`: public compiler headers.
- `src/support/`: memory, source loading, diagnostics, and utility code.
- `src/lex/`: token and lexer logic.
- `src/parse/`: AST and parser.
- `src/sema/`: semantic checking.
- `src/cli/`: command entry point.
- `src/asm/`: profiled hot-path assembly only.
- `cmake/`: cross-platform build and test scripts.
- `.github/workflows/`: CI build and verification workflows.
