# cnegative Language Spec

## Core Position

`cnegative` is explicit, readable, and low-level. It keeps manual control, reduces hidden behavior, and prefers words over symbolic shortcuts when that improves clarity.

## Current Syntax Direction

### Functions

```lang
fn:int main() {
    return 0;
}
```

Public functions use `pfn`.

```lang
pfn:int add(a:int, b:int) {
    return a + b;
}
```

Public structs use `pstruct`.

```lang
pstruct Point {
    x:int;
    y:int;
}
```

### Variables

```lang
let x:int = 10;
let mut y:int = 20;
```

Statement rule:

- Simple statements end with `;`.
- Struct fields end with `;`.
- Block forms such as `if`, `while`, `for`, `loop`, `fn`, and `struct` do not take a trailing `;` after the closing `}`.

### Primitive Types

- `int`
- `bool`
- `str`
- `void`

Implemented composite type forms:

- `ptr T`
- `result T`

### Control Flow

```lang
if x > 5 {
    print(x);
}
```

Strict condition rule:

- A control-flow condition must type-check to `bool`.
- `if x > 5 {}` is valid because `x > 5` is boolean.
- `if x {}` is invalid when `x` is not `bool`.
- No implicit integer truthiness exists.

### Loops

```lang
while x < 10 {
    x = x + 1;
}
```

```lang
for i:int in 0..10 {
    print(i);
}
```

```lang
loop {
}
```

### Returns

Explicit return is required in non-void functions:

```lang
fn:int add(a:int, b:int) {
    return a + b;
}
```

### Modules

Current local module rule:

```lang
import math as m;

fn:int main() {
    return m.add(2, 3);
}
```

`import name;` resolves `name.cneg` relative to the importing file. Only `pfn` functions are callable from another module. The loader still accepts legacy `.cn` imports during the transition.

Imported struct types are also available through a qualified name:

```lang
import shapes as s;

fn:int main() {
    let p:s.Point = s.Point {
        x:1,
        y:2
    };

    return p.x;
}
```

The module must still be imported first. `shapes.Point` without a matching `import shapes;` is rejected. Cross-module struct usage only works for `pstruct` declarations.

### Pointer and Result Forms

```lang
let mut x:int = 10;
let p:ptr int = addr x;
p.value = 11;
deref p = 12;

let heap:ptr int = alloc int;
heap.value = x;
deref heap = x;
free heap;
```

```lang
fn:result int divide(a:int, b:int) {
    if b == 0 {
        return err;
    }

    return ok a / b;
}
```

Current checked rule for `result` access:

- `.ok` is always readable.
- `.value` requires proof in the current scope that the named result is ok.
- `if r.ok { return r.value; }` is valid.
- `return r.value;` without such a guard is rejected.

## Current Pipeline

```text
Source
-> Lexer
-> Parser
-> AST
-> Semantic Analysis
-> Typed IR
-> LLVM IR
-> Object File
-> Static Linking
-> Binary
```

`build/cnegc ir <file>` dumps the checked project as typed IR. The current IR keeps structured control flow, preserves explicit returns, and resolves module-qualified calls to canonical module names.

`build/cnegc llvm-ir <file>` lowers the checked subset into textual LLVM IR. The current backend handles `int`, `bool`, `str`, arrays, structs, `ptr`, `result`, structured control flow, local bindings, allocation/free, local calls, and imported module calls. `build/cnegc obj <file> [output]` emits an object file, and `build/cnegc build <file> [output]` links a runnable binary through `clang-18` or `clang`.

Current runtime notes:

- `input()` returns an owned heap-backed string copy in the generated runtime helper.
- `free some_string;` releases tracked owned strings created by `input()`. Freeing string literals is a safe no-op in the generated runtime.
- `str` equality in the backend is content-based through `strcmp`, not pointer-identity based.
