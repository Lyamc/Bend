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
   On a machine without either, `winget install --id=LLVM.LLVM -e` installs
   LLVM, or `winget install --id=MartinStorsjo.LLVM-MinGW.UCRT -e` installs
   the MinGW-build clang that the CLI retargets to MSVC. Put the new `bin`
   directory on `PATH` *ahead* of any other clang: a Swift toolchain, for
   one, ships an old clang 17, and a GPU `!` build needs 19+. `$CC` names
   one compiler when the search order is not enough.
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
  touch. Worker stacks reserve 1 GiB of address space each (upstream Unix
  reserves 2 GiB); only the touched pages are committed. The guard page is a
  memory fault: a vector exception handler prints
  `bend: memory fault (machine stack overflow?)` after flushing stdout and
  exits 1, matching the Unix lanes.
- A MinGW-default clang (MSYS2's `x86_64-w64-windows-gnu`) works too: the
  CLI detects that triple and adds `--target=x86_64-pc-windows-msvc
  -rtlib=platform`, because the generated C links with `#pragma comment` and
  uses `clock_gettime`, both MSVC-target only.
- `clang` is invoked with `-std=c11 -O3 -Wl,/STACK:268435456`. Extra libs
  (`ws2_32`, `user32`, `gdi32`, `winmm`, `bcrypt`) are pulled in with
  `#pragma comment(lib, ...)`.
- The CRT's errno numbers differ from POSIX in a few places; the shim
  renumbers `EILSEQ`, `ENOTSUP`, `EOVERFLOW`, `EWOULDBLOCK` and `ENOSYS` to
  the Linux values the tests and the JS lane already speak.
- `/dev/null`, `/dev/zero` (reads answer zeros, writes vanish), and
  `/dev/stdin` / `/dev/stdout` / `/dev/stderr` (a `dup` of the process's
  descriptor) are mapped. `/etc/hosts` resolves to
  `%SystemRoot%\System32\drivers\etc\hosts`. `_WIN32` `File.read` caps its
  buffer at the regular file's remaining size, since the Windows CRT commits
  a `malloc` eagerly; `pwrite` past end of file marks the file sparse first
  (NTFS otherwise fills the hole, which is slow for a write a gigabyte out).
- The JS lane tracks `O_APPEND` descriptors by wrapping `node:fs`
  `openSync`/`closeSync`: a Bun fd is not in the CRT table `bun:ffi` loads,
  and asking `_get_osfhandle` about one aborts the process.
- Redirected stdout can be a socket (the pipe shim used by tests); the shim
  redefines `fwrite` to send through it for a socket descriptor, because the
  CRT's `fwrite`/`_write` drives sockets with `WriteFile`, which they reject.
- A redirected standard stream is binary: a pipe, file or socket must carry
  the same bytes the interpreter prints, so the C lane does not turn `\n`
  into `\r\n` (nor a CRLF read into `\n`). An attached console keeps text
  mode, so its lines still start in column 0. Without this, every `#|` line
  of every native test differed by a `\r` when the suite captured output.
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
  `%LIB%`; the build then passes that full path. Without `%LIB%` or
  `%VCToolsInstallDir%`, it asks `vswhere` for the newest Visual Studio and
  searches its toolsets, so a plain PowerShell with LLVM on `PATH` links
  too. MSYS2 clang does not search `%LIB%` for a bare `delayimp.lib`; from an
  MSYS2 shell, run the VS developer environment first, or the link cannot
  see the import library.

- The winmm pump feeds `waveOut` 256 float32 stereo frames at a time. Its
  `WAVEHDR` and its chunk live in the ring, not on the pump thread's stack,
  and stopping calls `waveOutReset` before unpreparing. `waveOutWrite`
  returns while the device still reads the chunk, so a stack copy was freed
  under the driver: opening and closing an `Audio` at 48 kHz died with an
  access violation about half the time after the close. The header is
  cancelled (`waveOutReset` marks every pending buffer done) before it is
  unprepared, and `io_ring_free` resets before joining the pump.

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
`ERROR_BROKEN_PIPE` and `ERROR_NO_DATA` to `EPIPE`. The JS lane opens the
same named pipe through `_open_osfhandle` and keeps its own fd table, so it
closes with the CRT's `_close` (a `node:fs` close would target an unrelated
Bun fd and leak the pipe). The `OVERLAPPED.hEvent` field sits at offset 24
on x64 and 16 on x86; the event must be there or the kernel queues the
completion where nobody waits for it and a connect or read never wakes.

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

## Demos

All 16 upstream `demos/` build natively (`bend demos/<dir>/main.bend -o
<name>.exe`) and run:

| Demo | Run |
| --- | --- |
| `io_hello_world` | prints `Hello, world!` |
| `io_tcp_echos` | echoes on `127.0.0.1:7777` |
| `io_http_server` | answers `127.0.0.1:8080` |
| `io_http_fetch` | fetches ip-api.com and prints status, country, city, IP |
| `io_rollback_netcode` | the relay plus two headless bots print the same three `posts=/tick=/hash=` lines; `walkers_demo.bend` draws a walker against that relay |
| `app_win_is_bug_2d` | prints the board and `still not won` |
| `app_triangle_2d`, `app_pong_game_2d`, `app_ray_tracer_3d`, `app_slash_boss_3d` | open a Win32 window and draw frames (the ray tracer's GPU `!` included) |
| `pure_par_sum` | `2147450880` |
| `pure_par_sort` | `sorted` |
| `pure_hvm5_mini` | `&S{#0{()},#1{()}}` and `- Itrs: 79 interactions` |
| `proof_insertion_sort`, `proof_numerics`, `proof_typed_eval` | `[1n, 2n, 3n]`, `(146n, 2n)`, `13n` |

Slash Boss 3D runs headless from the repo root (its sounds resolve from
`media/slash_boss_3d`), and its probe prints the state, the cues and the
microseconds of each stage:

```powershell
$env:SLASH_PROBE=600
.\slash.exe
```

`SLASH_MODE`, `SLASH_DUMP`, `SLASH_VOICES` and `SLASH_WAV` work too; 600
frames with 4 voices and a WAV dump take about 7 s.

Every demo file that is meant to check alone passes `--check-only`. Two
kinds do not, for language reasons and on every platform: a `LAWS.bend`
fails while its TODOs are unfilled (`PROOF.bend` fills them and checks), and
`pure_hvm5_mini` and `io_rollback_netcode` report their `@unsafe` defs.

## Tests

`gates/test.ts` runs on the Linux mini cluster, so the suite was mirrored
locally with the same lanes and judging (per-lane `-o` builds, `#|` lines,
a build that fails leaving its message as the lanes' answer, and one
directory per test holding `bend2` and `tests` as junctions). The last run
built and ran every runnable test natively: 1842 of 1846, with the five
`# windows: skip` files above and the four known-difference files below
(`tcp_text_residue` and the three `gfx_*`). Everything else — base, check,
comptime, eval, flatten, import, io, parse, printer, proof, reg, run, state,
stuck, and the rest — matches the `#|` lines on all three lanes.

## Still skipped

These start with `# windows: skip`. On Windows, `bend file.bend` prints
`bend: skipped on windows: ...` and does not run them.

- `tests/proof/app/main.bend`, an endless loop the Linux suite never runs
- `Process.run` of `/bin/*` (`process_run`, `process_run_no_shell`,
  `process_run_non_scalar`, `process_run_parallel`,
  `process_run_node_module`)
- `bundle_inline` pages whose names contain `"` (Windows file names cannot)

## Known differences

- `tests/io/tcp_text_residue` expects a 600 KB non-blocking send to a
  non-reading loopback peer to stop partway. Windows loopback accepts the
  whole string even with `SO_SNDBUF`/`SO_RCVBUF` pinned, so both lanes print
  `send did not wait` instead of the residual-send path. The byte-residue
  code is still exercised by `tcp_send_slow_peer`.
- The three `gfx_*` window tests need an interactive desktop and report an
  empty or stalled event run under the headless sweep.
- Bun 1.4.x's interpreter has crashed on a couple of FIFO writer tests
  (`file_read_short`, `fifo_eof`) in earlier runs; the current runtime passes
  them, but a Bun/JSC fault there would be a Bun issue, not Bend.

## Cross-building for Android

`--target android` builds a PIE for an arm64 phone (a Galaxy Note 10+, say);
`android-x86_64` and `android-arm` name the emulator and 32-bit ABIs. The
compiler finds the NDK at `$ANDROID_NDK_HOME`, `$ANDROID_NDK_ROOT`, or the
newest `ndk` under `$ANDROID_HOME` / `$ANDROID_SDK_ROOT` /
`%LOCALAPPDATA%\Android\Sdk`, and calls its `clang` with
`--target=<triple>21`, which finds its own sysroot:

```powershell
bend demos/io_hello_world/main.bend --target android -o hello
adb push hello /data/local/tmp/hello
adb shell chmod 755 /data/local/tmp/hello
adb shell /data/local/tmp/hello
```

The binary is ELF64 AArch64, PIE, and needs only bionic's `libc.so`,
`libm.so` and `libdl.so`: bionic keeps pthreads in libc, so the link passes
`-lm` and not `-lpthread`, and no `.exe` is added on Windows. API 21 is the
floor; the phone also needs USB debugging for `adb push`.

Pure code, `IO` (files, sockets, processes), threads and channels work. Two
effect kits do not, and the build says so instead of failing in the NDK's
headers:

- `!` GPU: there is no CUDA or Metal on a phone, so a `!` runs on its cores
  (the build prints that), as any non-Mac host without CUDA already does.
- `Window` / `Audio`: `window.c` is Cocoa or X11 and `audio.c` ALSA or
  winmm, so an Android backend (ANativeWindow + input, AAudio/Oboe) is still
  to write. Programs that import either refuse to cross-build.

Android 10+ refuses to `exec` a file in an app's data directory, so the
binary runs from `/data/local/tmp` through `adb`, or from Termux; shipping it
inside an app means loading it as a JNI library from an Activity.

## Updating from upstream

```powershell
git fetch https://github.com/bendlang/bend.git main
```

Replay the Windows patches onto the new tree rather than dropping them:
`bend2/effs/winposix.c`, `bend2/effs/io_sys_win.js`, path handling in
`bend.ts` / `main.ts`, the Win32 branches of `window.c` and `audio.c`, the
`CreateProcess` path in `process_run.c`, and the CLI CUDA / `.exe` logic.
