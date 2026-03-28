# Typed IR

`cnegative` now lowers checked source into a structured typed IR before any LLVM work starts.

## Current Properties

- Independent IR node types separate from the parser AST.
- Canonical module-qualified function and struct names in lowered output.
- Explicit return statements preserved from source.
- Structured control flow preserved for `if`, `while`, `loop`, and range `for`.
- No SSA, basic blocks, or LLVM-specific details yet.

## CLI

```sh
build/cnegc ir examples/valid_imported_structs.cneg
```

## Example Shape

```text
module valid_imported_structs (...) {
    fn valid_imported_structs.main() -> int {
        let p:shapes.Point = shapes.make_point(3, 4);
        return w.point.y;
    }
}
```

This stage is meant to stabilize typing and symbol resolution before control-flow lowering and LLVM emission.
