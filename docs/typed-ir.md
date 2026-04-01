# Typed IR

`cnegative` now lowers checked source into a structured typed IR before any LLVM work starts.

## Current Properties

- Independent IR node types separate from the parser AST.
- Builtin primitive types currently include `int`, `u8`, `bool`, `str`, and `void`.
- Composite types currently include arrays, `ptr T`, `result T`, and `slice T`.
- Module-level constant declarations lowered into canonical IR form.
- Canonical module-qualified function and struct names in lowered output.
- Canonical module-qualified public constants in lowered output.
- Builtin stdlib calls preserved as canonical module-qualified builtin targets such as `std.math.gcd(...)`, `std.bytes.append(...)`, `std.strings.concat(...)`, `std.text.build(...)`, `std.io.write_line(...)`, `std.time.now_ms(...)`, `std.env.get(...)`, `std.path.extension(...)`, `std.fs.file_size(...)`, `std.net.tcp_connect(...)`, `std.net.udp_recv_from(...)`, and `std.process.platform(...)`.
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

Builtin stdlib imports remain visible in the dumped IR as explicit calls, for example:

```text
let joined:str = std.strings.concat(copied, "2");
let before:int = std.time.now_ms();
std.time.sleep_ms(20);
let parsed:result int = std.parse.to_int(text.value);
std.io.write_line("ready");
let env_value:result str = std.env.get("PATH");
let path_value:str = std.path.join("build", "demo.txt");
let cwd:result str = std.fs.cwd();
let endpoint:str = std.net.join_host_port("127.0.0.1", 8080);
let socket:result int = std.net.tcp_connect("127.0.0.1", 34567);
if socket.ok {
    let payload:result str = std.net.recv(socket.value, 32);
}
let udp_socket:result int = std.net.udp_bind("", 34567);
if udp_socket.ok {
    let packet:result std.net.UdpPacket = std.net.udp_recv_from(udp_socket.value, 64);
}
let remainder:int = sample % 6;
let bounded:int = std.math.clamp(99, 0, 7);
let factor:int = std.math.gcd(54, 24);
let platform:str = std.process.platform();
let view:slice int = slice data;
```

Current optimization pass behavior:

- folds constant integer and boolean expressions
- folds string literal equality and inequality
- trims unreachable statements after `return`
- optimizes module constant initializers before backend lowering

This stage is meant to stabilize typing, symbol resolution, and simple canonical simplification before LLVM emission.
