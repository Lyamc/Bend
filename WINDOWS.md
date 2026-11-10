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
- CUDA 13.x ships `nvrtc64_130_0.dll` in `%CUDA_PATH%\bin\x64`, which is not
  on PATH by default. A GPU link delay-loads whichever `nvrtc64_*.dll` is in
  that directory, and the runtime `LoadLibrary`s it from there before the
  first nvrtc call. Put that directory on PATH as well if something else
  loads nvrtc before `main`.
- That link also needs `delayimp.lib` from the MSVC toolset
  (`VC\Tools\MSVC\<version>\lib\x64`). A VS developer prompt puts this on
  `%LIB%`. The build passes that full path when `%LIB%` or
  `%VCToolsInstallDir%` already names the file. MSYS2 clang does not search
  `%LIB%` for a bare `delayimp.lib`; from an MSYS2 shell, run the VS
  developer environment first, or the link cannot see the import library.

## FIFOs

`mkfifo` of a POSIX path such as `/tmp/bend_fifo_1` does not create a file.
It registers that path. The pipe object is

```text
\\.\pipe\bend
```

plus the path with every `/`, `\`, and `:` replaced by `_`. The example is
`\\.\pipe\bend_tmp_bend_fifo_1`. The read end is `CreateNamedPipe` (byte
mode, overlapped, `PIPE_WAIT`). The write end is `CreateFile` on that name.
A read after the writer closes is end of file, as on Linux. A nonblocking
read of an empty pipe is `EAGAIN`. A write after the reader is gone maps
`ERROR_BROKEN_PIPE` and `ERROR_NO_DATA` to `EPIPE`.

## EINTR and EPIPE

The C tests that interrupt one call do it with a hook inside `win_open`,
`win_read`, `win_write`, `pwrite`, and the Winsock wrappers, because a
Windows build `#define`s those names onto the shim before the test is
compiled. `select` watches one extra loopback socket. `pthread_kill` writes
a byte there and the wait returns `EINTR` without marking the program's
fds. The JS runner arms the same result with a deadline on `WSAPoll`: when
it fires, the call returns `EINTR` and no fd is ready. Neither path is on
unless a test asks for it.

A write to a standard stream whose reader is gone is `EPIPE`. Windows has
no `SIGPIPE`, so that process exits 141, the status a shell reports for
`SIGPIPE`. `Epipe.write` runs the write in a child of the same executable
(there is no `fork` that can return into the parent computation).

## Still skipped

These start with `# windows: skip`. On Windows, `bend file.bend` prints
`bend: skipped on windows: ...` and does not run them.

- `tests/proof/app/main.bend`, an endless loop the Linux suite never runs
- `Process.run` of `/bin/*` (`process_run`, `process_run_no_shell`,
  `process_run_non_scalar`, `process_run_parallel`,
  `process_run_node_module`)
- `bundle_inline` pages whose names contain `"` (Windows file names cannot)

## Updating from upstream

```powershell
git fetch https://github.com/bendlang/bend.git main
```

Replay the Windows patches onto the new tree rather than dropping them:
`bend2/effs/winposix.c`, `bend2/effs/io_sys_win.js`, path handling in
`bend.ts` / `main.ts`, the Win32 branches of `window.c` and `audio.c`, the
`CreateProcess` path in `process_run.c`, and the CLI CUDA / `.exe` logic.
