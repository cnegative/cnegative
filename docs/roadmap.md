# Roadmap

Current priority order:

1. Keep expanding the standard library/runtime surface beyond the current `std.math`, `std.strings`, `std.parse`, `std.fs`, `std.io`, `std.time`, `std.env`, `std.path`, `std.net`, and `std.process` slice.
2. Continue the next compiler phase after the current frontend, typed IR, optimizer, LLVM pipeline, and initial stdlib surface are more stable.
3. Tighten release/install/tooling ergonomics around the current compiler workflow.

Recently completed:

- runtime-owned strings beyond `input()`
- module-level constants and broader top-level visibility groundwork
- parser recovery for continued syntax diagnostics
- typed IR optimization before LLVM lowering
- initial stdlib modules: `std.math`, `std.strings`, `std.parse`, `std.fs`, `std.io`, `std.time`, `std.env`, `std.path`, `std.net`, and `std.process`

Current intent:

- Focus next on broader stdlib/runtime breadth.
- Shift attention back to the next compiler phase after that.
