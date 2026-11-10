# Native Windows (this fork)

Upstream Bend 2 documents that Windows is unsupported (WSL works). This fork
tracks current [bendlang/bend](https://github.com/bendlang/bend) `main`
(2.0.40) and keeps the language, checker, JS runner and C emitter compatible
with upstream, while running them on native Windows.

Bend 1 programs and HVM do not carry over (that is an upstream language
break, not a fork choice). The earlier Lyamc Bend 1 / HVM2 Windows port is on
the `bend1-windows` branch.

## What works

| Path | Status |
| --- | --- |
| `bend file.bend` (check + JS run) | Yes. Needs [Bun](https://bun.sh) 1.2+. |
| `bend file.bend -o out.exe` (CPU) | Yes. Needs clang 14+ targeting `x86_64-pc-windows-msvc`. |
| TCP / UDP / files / print / channels | Yes (Winsock + CRT). |
| `Process.run` | Yes (`CreateProcess`, no shell). |
| `Window.open` / frames / fullscreen / grab | Yes (Win32). |
| `Audio.open` | Yes (winmm `waveOut`). |
| GPU `!` / CUDA | If clang 19+ (`#embed`) and CUDA 12+ at `%CUDA_PATH%`. |
| Metal | macOS only. |

## Install

1. Install [Bun](https://bun.sh) (`powershell -c "irm bun.sh/install.ps1 | iex"`).
2. For native binaries: [LLVM/clang](https://llvm.org/) 14+ (19+ for GPU) and
   Visual Studio 2022 with the Desktop C++ workload (the clang MSVC target
   links the VS CRT). For GPU, the CUDA toolkit, with `CUDA_PATH` set.
3. From this repo:

```powershell
.\install.ps1
bend --version
bend demos/io_hello_world/main.bend
bend demos/io_hello_world/main.bend -o hello
.\hello.exe
```

`install.ps1` writes `bend.cmd` next to `bun.exe` (`%USERPROFILE%\.bun\bin`)
so `bend` is on `PATH` after that directory is.

You can also run without installing:

```powershell
bun bend2/main.ts demos/io_hello_world/main.bend
```

## Build notes

- Generated C is one file. On Windows it embeds a POSIX shim
  (`bend2/effs/winposix.c`: pthreads, mmap-on-reserve, poll, sockets,
  `clock_gettime`, `pread`/`pwrite`).
- The CPU heap reserves virtual address space and commits pages on first
  touch. Worker stacks are 8 MiB (upstream Unix reserves 2 GiB per worker).
- `clang` is invoked with `-std=c11 -O3 -Wl,/STACK:268435456`. Extra libs
  (`ws2_32`, `user32`, `gdi32`, `winmm`, `bcrypt`) are pulled in with
  `#pragma comment(lib, ...)`.
- Clang's `musttail` / `preserve_none` crash on Windows x64, so the host
  evaluator uses the same switch loop as the GPU device path and does not
  grow the C stack. GPU `!` still needs clang 19+ and CUDA 12+.
- CUDA includes/libs are taken from `%CUDA_PATH%` / `%CUDA_HOME%`
  (`include`, `lib\x64`), not only `/usr/local/cuda`.

## Updating from upstream

```powershell
git fetch https://github.com/bendlang/bend.git main
```

Replay the Windows patches onto the new tree rather than dropping them:
`bend2/effs/winposix.c`, `bend2/effs/io_sys_win.js`, path handling in
`bend.ts` / `main.ts`, the Win32 branches of `window.c` and `audio.c`, the
`CreateProcess` path in `process_run.c`, and the CLI CUDA / `.exe` logic.
