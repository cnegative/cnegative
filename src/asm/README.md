# Assembly Hot Paths

This directory contains profiled hot-path code only.

Policy:

- Write the first version in clear C.
- Measure before rewriting. Use `build/cnegc bench-lexer <file> [iterations]` to baseline lexer throughput.
- Move only the hot section to assembly.
- Keep the public behavior identical to the C version.
- Current assembly-backed path: ASCII identifier and number tail scanning on x86_64 Linux and x86_64 macOS.
- Other hosts, including arm64 macOS, use the C fallback automatically.
