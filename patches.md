# Patches

## v0.5.1

- Made `result` a contextual keyword in type positions so it can be reused as a
  normal identifier or import alias elsewhere.
- Added targeted parse diagnostics for missing inner types after `result`,
  `ptr`, and `slice` prefixes.
- Expanded the `.value` proof system to understand immutable bool aliases of
  `.ok` and simple `&&` / `||` branch-proof patterns.

## v0.5.0

- Added source-level `null` and pointer equality against `null`.
- Allowed `main` to return `result int` and `result u8`, which makes top-level
  `try` usable in normal CLI programs.
- Added `std.strings.from_int(int)` and `std.text.append_int(...)` for direct
  integer-to-text formatting paths.
- Reworked printing so `print(...)` no longer appends a newline and
  `println(...)` is the newline form.
- Expanded arrays with constant-expression sizes and `[value; N]` repeat
  literals, and fixed the `ptr T[N]` parsing ambiguity so it now means
  `array[N] of ptr T`.
- Tightened result-guard handling for indexing and slicing after proven-ok
  flows.
- Added `zone { ... }` and `zalloc T` for explicit temporary scoped
  allocations, including phase-2 escape checks for returns, outer storage, and
  ordinary function-call boundaries.

## v0.4.4

- Reworked module loading so imports now resolve through a clear search order:
  builtin `std.*`, project root, `vendor/` under the project root, then a
  legacy relative fallback.
- Added canonical module names derived from root-relative paths, so modules like
  `feature.helper` and `shared.helper` no longer collapse to the same basename.
- Improved `E3017` diagnostics to report searched paths and canonical-name
  conflicts directly.
- Added regression fixtures and smoke coverage for project-root imports,
  vendor-root imports, legacy relative fallback, and missing-import diagnostics.

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
