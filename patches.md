# Patches

## v0.4.3

- Added compile-time memory diagnostics for invalid `addr` and `free` misuse:
  `E3035`, `E3036`, `E3037`, and `E3038`.
- Hardened the tracked allocator with counted allocation/reallocation overflow
  checks, double-free detection, interior-pointer `free` detection, quarantine
  backed use-after-free checks, and guard-based overflow/underflow reporting.
- Added dedicated runtime memory tests for normal allocation flow, double free,
  interior-pointer free, overflow, underflow, and quarantine-window
  use-after-free detection.
- Updated the language and docs references to document the current memory
  diagnostics and ownership rules more clearly.

## v0.4.2-p

- Fixed incorrect `defer` lowering across nested blocks so outer-scope cleanup
  no longer runs at the end of each loop iteration.
- Added a regression test for the nested `defer` case with
  `examples/valid_defer_loop.cneg` and smoke coverage.
- Reworked allocator bookkeeping to use hashed tracked-allocation lookup, which
  removed the previous minutes-long `check` slowdown on large generated inputs.
- Added faster semantic-analysis name lookup tables for scopes and module
  symbols.
- Kept the patch release focused on compiler/runtime fixes and performance, with
  no new user-facing language syntax beyond what already landed before
  `v0.4.2`.
