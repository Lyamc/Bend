<h1>Bend</h1>
<p>A high-level, massively parallel programming language</p>

This repository is an **independent fork** of [HigherOrderCO/Bend](https://github.com/HigherOrderCO/Bend), paired with [Lyamc/HVM2](https://github.com/Lyamc/HVM2). The goal is broader **platform support** (especially Windows, MSVC and GNU) and **better runtime performance** than the upstream main branch. Most of the porting, backends, and measurement work was done with AI assistance.

Upstream Bend and HVM remain the language and paper of record. This fork is not the official HigherOrderCO project.

## Index
1. [Introduction](#introduction)
2. [Important Notes](#important-notes)
3. [Install](#install)
4. [Getting Started](#getting-started)
5. [Performance (this machine)](#performance-this-machine)
6. [Speedup Example](#speedup-examples)
7. [Additional Resources](#additional-resources)

## Introduction

Bend has the feel of languages like Python and Haskell: fast object allocations, higher-order functions with closures, unrestricted recursion, and continuations.

It is meant to scale like CUDA on massively parallel hardware, without explicit parallelism annotations — no thread creation, locks, mutexes, or atomics in the source. A program that *can* run in parallel *will* run in parallel on a parallel backend.

The runtime is [HVM2](https://github.com/Lyamc/HVM2) (this fork). Upstream HVM2 is [HigherOrderCO/HVM](https://github.com/higherorderco/hvm).

## Important Notes

**Scaling**

- Designed to use thousands of concurrent workers when the program has independent work.
- Single-core performance is often weaker than a conventional compiled language.
- Code generation (`gen-c` / `gen-rs` / `gen-cu`) is still early compared to GCC or GHC.

**This fork vs upstream**

- Windows is a first-class target here (MSVC and GNU/MinGW). Upstream Bend/HVM2 historically assumed a Unix + NVIDIA stack.
- Extra backends on the same `.bend` files: `run-rs` (parallel Rust), `run-wgpu` (WebGPU / DX12 / Vulkan / Metal), `gen-rs` (standalone Rust).
- NVIDIA CUDA (`run-cu` / `gen-cu`) is still the faster *GPU HVM* path when `nvcc` is available.
- Handwritten PTX / Vulkan / wgpu kernels in the perf suite are **not** Bend; they are a separate baseline.

**Windows runtime**

- `bend run-rs` always works (parallel Rust interpreter).
- `bend run-c` needs HVM2’s C runtime: MSVC `cl` + C11 atomics, or MinGW `gcc -std=c11`. The C heap is several GB.
- Put `hvm.exe` on `PATH`, next to `bend.exe`, or in `%CARGO_HOME%\bin`.
- If rustc’s `lld-link` fails against the VS CRT: after `vcvars64`, set `RUSTFLAGS=-C linker=link.exe`.

## Install

### Install dependencies

#### On Linux
```sh
# Install Rust if you haven't already.
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# For the C runtime, GCC up to 12.x is a safe choice.
sudo apt install gcc
```
For CUDA, install the [CUDA toolkit for Linux](https://developer.nvidia.com/cuda-downloads?target_os=Linux) 12.x.

#### On Mac
```sh
# Install Rust if you haven't already.
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# For the C runtime, GCC up to 12.x is a safe choice.
brew install gcc
```

#### On Windows (MSVC)
```bat
:: Visual Studio 2022 with Desktop C++ (for cl.exe / link.exe)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
:: If rustc's lld-link fails against the VS CRT:
set RUSTFLAGS=-C linker=link.exe
```

Keep `PATH` short before `vcvars64` if you see `The input line is too long`.

#### On Windows (GNU / MinGW)
```bat
rustup target add x86_64-pc-windows-gnu
set CC=gcc
```

`--release` / `cargo install` uses `opt-level = 3`, fat LTO, `codegen-units = 1`, and `strip = true`. Windows targets also pass `/OPT:REF /OPT:ICF` (MSVC) or `--gc-sections` (GNU).

### Install Bend

1. Install HVM2 (this fork):
```sh
cargo install --git https://github.com/Lyamc/HVM2 --locked
hvm --version
```
For WebGPU as well: `cargo install --git https://github.com/Lyamc/HVM2 --locked --features wgpu`.

2. Install Bend (this fork):
```sh
cargo install --git https://github.com/Lyamc/Bend --locked
bend --version
```

On Windows, add both install dirs to `PATH` or copy `hvm.exe` next to `bend.exe`. `bend` also looks in `%CARGO_HOME%\bin` (default `%USERPROFILE%\.cargo\bin`). Use `bend run-rs` if the C runtime was not built.

For a GNU build, add `--target x86_64-pc-windows-gnu` to both `cargo install` commands.

## Getting Started

### Running Bend programs

```sh
# Interpreters (all can use multiple workers; the program decides if work is parallel)
bend run      <file.bend>   # C interpreter (default; parallel)
bend run-rs   <file.bend>   # Rust interpreter (parallel)
bend run-c    <file.bend>   # C interpreter (parallel)
bend run-cu   <file.bend>   # CUDA interpreter (massively parallel; NVIDIA)
bend run-wgpu <file.bend>   # WebGPU interpreter (same .bend files)

# Standalone compilers (print C / Rust / CUDA to stdout; then compile and run the result)
bend gen-c    <file.bend>   # standalone C
bend gen-rs   <file.bend>   # standalone Rust
bend gen-cu   <file.bend>   # standalone CUDA (same interpreted CUDA book as run-cu)
bend gen-hvm  <file.bend>   # HVM book only
```

`-s` prints reduction count, wall time, and millions of interactions per second.

The generators are still early. `gen-c` / `gen-rs` compile the interaction rules; `gen-cu` currently embeds the same interpreted CUDA runtime as `run-cu`.

Example after `gen-c` / `gen-rs` / `gen-cu`:

```sh
# C (needs cl or gcc; several GB of RAM)
bend gen-c parallel_sum.bend > parallel_sum.c
cl /O2 /std:c11 /experimental:c11atomics parallel_sum.c /Fe:parallel_sum.exe
# or: gcc -O2 -std=c11 parallel_sum.c -o parallel_sum
./parallel_sum

# Rust
bend gen-rs parallel_sum.bend > parallel_sum.rs
rustc --edition 2021 -C opt-level=3 -C lto=thin -C codegen-units=1 -C target-cpu=native --crate-name parallel_sum parallel_sum.rs
./parallel_sum

# CUDA (nvcc on PATH; same runtime as run-cu)
bend gen-cu parallel_sum.bend > parallel_sum.cu
nvcc -O3 -std=c++17 parallel_sum.cu -o parallel_sum
./parallel_sum
```

`HVM_THREADS` (or `hvm run --threads N`) sets live CPU workers, 1–16. The default is `min(8, 2^floor(log2(physical cores)))`.

### Testing Bend programs

The examples below sum every integer from `start` to `target`. One version is inherently sequential (each step needs the last). The other splits the range so independent halves can run at the same time. `-s` is used so reductions and time are visible.

The *runtime* is not sequential vs parallel here — `run-rs`, `run-c`, `run-cu`, and `run-wgpu` all have parallel evaluators. A sequential *algorithm* still runs on one logical chain of work.

#### Sequential version

Create `sequential_sum.bend`:

```py
# Defines the function Sum with two parameters: start and target
def Sum(start, target):
  if start == target:
    # If the value of start is the same as target, returns start.
    return start
  else:
    # If start is not equal to target, recursively call Sum with
    # start incremented by 1, and add the result to start.
    return start + Sum(start + 1, target)

def main():
  # This translates to (1 + (2 + (3 + (...... + (999999 + 1000000)))))
  # Note that this will overflow the maximum value of a number in Bend
  return Sum(1, 1_000_000)
```

Run it (the algorithm is sequential on every backend):

```sh
bend run-rs sequential_sum.bend -s
bend run-c  sequential_sum.bend -s
bend run-cu sequential_sum.bend -s
bend run-wgpu sequential_sum.bend -s
```

Each next sum depends on the previous one, so extra workers do not help.

#### Parallelizable version

Create `parallel_sum.bend`:

```py
# Defines the function Sum with two parameters: start and target
def Sum(start, target):
  if start == target:
    # If the value of start is the same as target, returns start.
    return start
  else:
    # If start is not equal to target, calculate the midpoint (half),
    # then recursively call Sum on both halves.
    half = (start + target) / 2
    left = Sum(start, half)  # (Start -> Half)
    right = Sum(half + 1, target)
    return left + right

# A parallelizable sum of numbers from 1 to 1000000
def main():
  # This translates to (((1 + 2) + (3 + 4)) + ... (999999 + 1000000)...)
  return Sum(1, 1_000_000)
```

`(3 + 4)` does not depend on `(1 + 2)`, so both can run at the same time.

```sh
bend run-rs parallel_sum.bend -s
bend run-c  parallel_sum.bend -s
bend run-cu parallel_sum.bend -s
bend run-wgpu parallel_sum.bend -s

# Compiled (usually faster on CPU than the interpreters)
bend gen-c  parallel_sum.bend > parallel_sum.c
bend gen-rs parallel_sum.bend > parallel_sum.rs
```

If the code **can** run in parallel, a parallel backend **will** run it in parallel. Switching from `run-rs` to `run-c` or `run-cu` does not rewrite the program; it changes the evaluator.

## Performance (this machine)

Measured on **Windows**, **RTX 3060 (sm_86)**, from `C:\Build\perf_test` (Lyamc/Bend 0.2.38 + Lyamc/HVM2 2.0.22). Hello/fib columns are **wall** (process start and GPU/device setup). Sieve/sort columns are HVM **`TIME` / MIPS** (evaluator only). Same `bend/*.bend` sources, no program edits.

| Program | What it does | Bend result | Native GPU result |
|---|---|---|---|
| hello | print a string | `"Hello, World!"` | `Hello, World!` |
| fibonacci | iterative Fib **n=42** | **`16256056`** (HVM **u24** wrap) | **`267914296`** |
| prime_sieve | primes ≤ **1e6** | **78498** | **78498** |
| sorting | Bend: bitonic **2^18** LCG tree; native GPU: **1e6** LCG ints | **`(13, 16777104)`** | first **3842**, last **2147480074** |

**Sort is not the same problem** on Bend vs the handwritten GPU kernels. Compare Bend backends to each other on sort; compare GPU natives to each other on the 1e6 LCG sort.

### Bend / HVM2 interpreters and compilers

CPU backends default to **8** workers (`min(8, 2^floor(log2(physical)))`). CUDA is 16 384 threads. Hello/fib are wall ms.

| Backend | Kind | hello | fib | sieve | sort |
|---|---|---:|---:|---|---|
| `run` / `run-rs` | interpreted Rust | 8 ms | 6 ms | **2.03 s** / 381 MIPS | **8.16 s** / 188 MIPS |
| `run-c` | interpreted C (TPC=16, 8 live) | 7 ms | 6 ms | **2.12 s** / 366 MIPS | **9.23 s** / 166 MIPS |
| `run-cu` | interpreted CUDA | 434 ms | 411 ms | **36.07 s** / 21 MIPS | **6.94 s** / 221 MIPS |
| `run-wgpu` | interpreted WebGPU (4096 lanes) | 795 ms | 831 ms | DNF (OOM / TDR / not competitive) | not run |
| `gen-c` | compiled C (`cl /O2`) | 11 ms | 9 ms | **1.47 s** / 529 MIPS | **5.32 s** / 289 MIPS |
| `gen-rs` | compiled Rust (`rustc -O -C lto=thin -C target-cpu=native`) | 9 ms | 9 ms | **1.79 s** / 434 MIPS | **6.13 s** / 251 MIPS |
| `gen-cu` | standalone CUDA (same interpreter as `run-cu`) | 427 ms | 418 ms | **36.16 s** / 21 MIPS | **6.97 s** / 220 MIPS |

Compile time (not in the run columns): `gen-c` ~0.3–0.4 s, `gen-rs` ~1.4–4.4 s, `gen-cu` ~18.5 s (`nvcc`).

`run-c` sieve was **8.54 s** in the suite pass (same 775 M ITRS) and **2.12 s** on an immediate `--threads 8` repeat. `run-rs` at `--threads 16` was sieve **1.60 s** / 485 MIPS, sort **5.38 s** / 286 MIPS.

- **Fastest Bend sieve** in the suite is **`gen-c` (1.47 s)**, then **`gen-rs` (1.79 s)** and **`run-rs` (2.03 s)**.
- **Fastest Bend sort** in the suite is **`gen-c` (5.32 s)**, then **`gen-rs` (6.13 s)**, then **`run-cu` / `gen-cu` (~6.9 s)**.
- **`run-rs` is in the same band as `run-c`** (lock-free pool, C-style local hbag).
- **`run-cu` / `gen-cu` pay ~0.4 s** just to allocate the CUDA net. They win on the *balanced* bitonic sort vs the interpreted CPU paths, not on the sieve.
- **`gen-cu` is not a faster CUDA evaluator** — it embeds the same interpreted CUDA runtime as `run-cu`.
- **`run-wgpu`** hello/fib match ITRS; sieve/sort still DNF. Same `.bend` files, no program edits.

### Other GPU programs in the same suite (not Bend)

These are handwritten kernels for the *native* problems (full Fib 42, 1e6 sieve, 1e6 LCG sort). Times are **wall ms** and are mostly **device init**, not kernel work.

| Implementation | hello | fib | sieve 1e6 | sort 1e6 |
|---|---:|---:|---:|---:|
| Handwritten PTX (CUDA sm_86 + `ptx_run`) | 133 ms | 149 ms | 132 ms | 141 ms |
| Vulkan (ash / SPIR-V) | 162 ms | 164 ms | 187 ms | 396 ms |
| wgpu 30 (WGSL compute) | 548 ms | 558 ms | 577 ms | 590 ms |

PTX and Vulkan finish the real 1e6 sieve/sort in well under the process-start tax. wgpu’s ~0.6 s is almost all adapter/device setup. None of these run Bend or HVM.

## Speedup Examples

The snippet below is a [bitonic sorter](https://en.wikipedia.org/wiki/Bitonic_sorter) with *immutable tree rotations*. That is not the usual “GPU-friendly” algorithm, but it is divide-and-conquer, so Bend can run it on many threads without thread APIs or locks.

### Bitonic Sorter Benchmark (upstream)

See [Performance (this machine)](#performance-this-machine) for numbers from this Windows / RTX 3060 suite. Upstream (HigherOrderCO) reported:

- `bend run-rs`: CPU, Apple M3 Max: 12.15 seconds
- `bend run-c`: CPU, Apple M3 Max: 0.96 seconds
- `bend run-cu`: GPU, NVIDIA RTX 4090: 0.21 seconds

Note: those upstream `run-rs` numbers predate the parallel Rust pool in this fork. Here, `run-rs` is a parallel interpreter.

<details>
<summary><b>Bitonic sorter code</b></summary>

```py
# Sorting Network = just rotate trees!
def sort(d, s, tree):
  switch d:
    case 0:
      return tree
    case _:
      (x,y) = tree
      lft   = sort(d-1, 0, x)
      rgt   = sort(d-1, 1, y)
      return rots(d, s, (lft, rgt))

# Rotates sub-trees (Blue/Green Box)
def rots(d, s, tree):
  switch d:
    case 0:
      return tree
    case _:
      (x,y) = tree
      return down(d, s, warp(d-1, s, x, y))

# Swaps distant values (Red Box)
def warp(d, s, a, b):
  switch d:
    case 0:
      return swap(s ^ (a > b), a, b)
    case _:
      (a.a, a.b) = a
      (b.a, b.b) = b
      (A.a, A.b) = warp(d-1, s, a.a, b.a)
      (B.a, B.b) = warp(d-1, s, a.b, b.b)
      return ((A.a,B.a),(A.b,B.b))

# Propagates downwards
def down(d,s,t):
  switch d:
    case 0:
      return t
    case _:
      (t.a, t.b) = t
      return (rots(d-1, s, t.a), rots(d-1, s, t.b))

# Swaps a single pair
def swap(s, a, b):
  switch s:
    case 0:
      return (a,b)
    case _:
      return (b,a)

# Testing
# -------

# Generates a big tree
def gen(d, x):
  switch d:
    case 0:
      return x
    case _:
      return (gen(d-1, x * 2 + 1), gen(d-1, x * 2))

# Sums a big tree
def sum(d, t):
  switch d:
    case 0:
      return t
    case _:
      (t.a, t.b) = t
      return sum(d-1, t.a) + sum(d-1, t.b)

# Sorts a big tree
def main:
  return sum(20, sort(20, 0, gen(20, 0)))
```

</details>

More algorithms: [upstream examples](https://github.com/HigherOrderCO/Bend/tree/main/examples).

## Additional Resources

- HVM2 [paper](https://paper.higherorderco.com/) (the runtime behind Bend)
- Language walkthrough: [GUIDE.md](https://github.com/HigherOrderCO/Bend/blob/main/GUIDE.md) (upstream)
- Feature list: [FEATURES.md](https://github.com/HigherOrderCO/Bend/blob/main/FEATURES.md) (upstream)
- Upstream project: [HigherOrderCO](https://higherorderco.com/) / [Discord](https://discord.higherorderco.com)
- This fork: [Lyamc/Bend](https://github.com/Lyamc/Bend) and [Lyamc/HVM2](https://github.com/Lyamc/HVM2)
