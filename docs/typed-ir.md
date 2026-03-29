# Typed IR

`cnegative` now lowers checked source into a structured typed IR before any LLVM work starts.

## Current Properties

- Independent IR node types separate from the parser AST.
- Module-level constant declarations lowered into canonical IR form.
- Canonical module-qualified function and struct names in lowered output.
- Canonical module-qualified public constants in lowered output.
- Explicit return statements preserved from source.
- Structured control flow preserved for `if`, `while`, `loop`, and range `for`.
- Simple optimization passes run before later backend stages.
- No SSA, basic blocks, or LLVM-specific details yet.

## CLI

```sh
build/cnegc ir examples/valid_consts_strings.cneg
```

## Example Shape

```text
module valid_consts_strings (...) {
    const valid_consts_strings.LOCAL:int = 20;

    fn valid_consts_strings.main() -> int {
        let joined:str = str_concat("hello", " world");
        if true {
            print(joined);
        }
        return 20;
    }
}
```

Current optimization pass behavior:

- folds constant integer and boolean expressions
- folds string literal equality and inequality
- trims unreachable statements after `return`
- optimizes module constant initializers before backend lowering

This stage is meant to stabilize typing, symbol resolution, and simple canonical simplification before LLVM emission.
