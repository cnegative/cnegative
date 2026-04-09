# Patches

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
