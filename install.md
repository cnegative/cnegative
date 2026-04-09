# Install `cnegc`

This guide covers installing the prebuilt `cnegc` compiler from GitHub Releases.

Release downloads:

- https://github.com/cnegative/cnegative/releases

Pick the archive that matches your platform:

- Linux: `cnegc-<tag>-linux-x86_64.zip`
- macOS: `cnegc-<tag>-macos-arm64.zip`
- Windows: `cnegc-<tag>-windows-x86_64.zip`

## What You Get

The release archive contains:

- `cnegc` or `cnegc.exe`
- `LICENSE`
- `README.md`
- `docs/how-to-run-and-build.md`

`cnegc` is enough for:

- `check`
- `ir`
- `llvm-ir`

If you want to build native output with `obj` or `build`, you also need:

- `clang-18` or `clang` in `PATH`

## Linux

Download and unpack:

```sh
unzip cnegc-v0.4.3-linux-x86_64.zip
```

Move the compiler somewhere on `PATH`, for example:

```sh
mkdir -p "$HOME/.local/bin"
cp release/cnegc/cnegc "$HOME/.local/bin/"
chmod +x "$HOME/.local/bin/cnegc"
```

If needed, add this to your shell config:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Check it:

```sh
cnegc
```

## macOS

Download and unpack:

```sh
unzip cnegc-v0.4.3-macos-arm64.zip
```

Move the compiler somewhere on `PATH`, for example:

```sh
mkdir -p "$HOME/.local/bin"
cp release/cnegc/cnegc "$HOME/.local/bin/"
chmod +x "$HOME/.local/bin/cnegc"
```

If needed, add this to your shell config:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Check it:

```sh
cnegc
```

## Windows

Download and unpack:

- Extract `cnegc-v0.4.3-windows-x86_64.zip`

Move `cnegc.exe` to a stable folder, for example:

```text
C:\tools\cnegc\
```

Add that folder to your user `PATH`.

Quick PowerShell check:

```powershell
cnegc.exe
```

If you want the shorter command:

```powershell
cnegc
```

that also works once the folder is on `PATH`.

## Next Step

After install, continue here:

- [How To Run And Build](docs/how-to-run-and-build.md)

That guide covers:

- compiler commands
- when `clang` is required
- building from source
- object and binary generation
