# Roadmap

Current priority order:

1. Expand the standard library/runtime surface now that owned strings are no longer limited to `input()`.
2. Continue the next compiler phase after the current frontend, typed IR, optimizer, and LLVM pipeline are more stable.
3. Tighten release/install/tooling ergonomics around the current compiler workflow.

Recently completed:

- runtime-owned strings beyond `input()`
- module-level constants and broader top-level visibility groundwork
- parser recovery for continued syntax diagnostics
- typed IR optimization before LLVM lowering

Current intent:

- Focus next on stdlib/runtime breadth.
- Shift attention back to the next compiler phase after that.
