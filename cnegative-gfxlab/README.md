# cnegative-gfxlab

`cnegative-gfxlab` is a Linux/X11-only `cnegative` graphics stress test.

This directory is now only for the real X11 experiment:

- real window open/pump/close on Linux
- tiny OS-facing stress testing without general FFI
- pressure-testing `std.x11` as the first real graphics/windowing path

Right now this only works on Linux with X11 available.

## How to run

From the repository root:

```sh
./build/cnegc check cnegative-gfxlab/x11_demo.cneg
./build/cnegc build cnegative-gfxlab/x11_demo.cneg build/cnegative-gfxlab-x11
./build/cnegative-gfxlab-x11
echo $?
```

Current behavior:

- the X11 demo opens a real window for about three seconds or until you close it
- the program exits `0` on the normal path
- if X11 setup fails, the demo returns a non-zero exit code
