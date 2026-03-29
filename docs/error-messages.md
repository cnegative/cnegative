# Error Messages

`cnegative` diagnostics use stable error codes so the language and tooling can be documented consistently.

## Parse Errors

- `E1001`: expected token missing in the current grammar position.
- `E1002`: unexpected token for the current grammar rule.
- `E1003`: invalid type syntax.
- `E1004`: invalid character during lexing.
- `E1005`: unterminated string literal.

## Semantic Errors

- `E3001`: duplicate function name.
- `E3002`: unknown name.
- `E3003`: duplicate local binding in the same scope.
- `E3004`: type mismatch.
- `E3005`: control-flow condition is not `bool`.
- `E3006`: assignment to immutable binding.
- `E3007`: non-void function does not return explicitly on every path.
- `E3008`: incorrect function call arity.
- `E3009`: unknown or invalid field access.
- `E3010`: invalid indexing target.
- `E3011`: array literal size mismatch.
- `E3012`: unknown declared type, struct name, or module-qualified type without a matching import.
- `E3013`: duplicate struct name.
- `E3014`: invalid call target or unsupported module-as-value usage.
- `E3015`: `err` used without an expected `result` type.
- `E3016`: duplicate import alias.
- `E3017`: module file could not be resolved or loaded.
- `E3018`: cyclic module import.
- `E3019`: `free` requires a pointer or string value.
- `E3020`: internal typed IR lowering invariant failed after semantic analysis.
- `E3021`: LLVM IR backend does not support the requested checked feature yet.
- `E3022`: external backend toolchain step failed.
- `E3023`: public API exposes a private type.
- `E3024`: `result.value` is used without a proven-ok guard.
- `E3025`: module-level constant initializer uses a runtime-only operation.
- `E3026`: cyclic module-level constant definition.
- `E3027`: duplicate or conflicting top-level constant name.

## Diagnostic Style

- Show source path, line, and column.
- Use one clear primary sentence.
- Prefer describing both expected and actual types.
- Reject ambiguous truthiness in conditions with `E3005`.
- Report missing or unknown struct fields directly at the literal or access site.
- Continue parsing after common syntax mistakes when recovery is safe so multiple real errors can be reported in one pass.
