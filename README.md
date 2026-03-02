# CCC — Claude's C Compiler

A C compiler written entirely from scratch in Rust, targeting x86-64, i686,
AArch64, and RISC-V 64. Zero compiler-specific dependencies — the frontend,
SSA-based IR, optimizer, code generator, peephole optimizers, assembler,
linker, and DWARF debug info generation are all implemented from scratch.
Claude's C Compiler produces ELF executables without any external toolchain.

> Note: With the exception of this one paragraph that was written by a human, 100% of the code and documentation in this repository was written by Claude Opus 4.6. A human guided some of this process by writing test cases that Claude was told to pass, but never interactively pair-programmed with Claude to debug or to provide feedback on code quality. As a result, I do not recommend you use this code! None of it has been validated for correctness. Claude wrote this exclusively on a Linux host; it probably will not work on MacOS/Windows — neither I nor Claude have tried. The docs may be wrong and make claims that are false. See [our blog post](https://anthropic.com/engineering/building-c-compiler) for more detail.

## Prerequisites

- **Rust** (stable, 2021 edition) — install via [rustup](https://rustup.rs/)
- **Linux host** — the compiler targets Linux ELF executables and relies on
  Linux system headers / C runtime libraries (glibc or musl) being installed
  on the host
- For cross-compilation targets (ARM, RISC-V, i686), the corresponding
  cross-compilation sysroots should be installed (e.g.,
  `aarch64-linux-gnu-gcc`, `riscv64-linux-gnu-gcc`)

## Building

```bash
cargo build --release
```

This produces five binaries in `target/release/`, all compiled from the same
source. The target architecture is selected by the binary name at runtime:

| Binary | Target |
|--------|--------|
| `ccc` | x86-64 (default) |
| `ccc-x86` | x86-64 |
| `ccc-arm` | AArch64 |
| `ccc-riscv` | RISC-V 64 |
| `ccc-i686` | i686 (32-bit x86) |

## Quick Start

Compile and run a simple C program:

```bash
# Write a test program
cat > hello.c << 'EOF'
#include <stdio.h>
int main(void) {
    printf("Hello from CCC!\n");
    return 0;
}
EOF

# Compile and run (x86-64)
./target/release/ccc -o hello hello.c
./hello

# Cross-compile for AArch64 and run under QEMU
./target/release/ccc-arm -o hello-arm hello.c
qemu-aarch64 -L /usr/aarch64-linux-gnu ./hello-arm
```

CCC works as a drop-in GCC replacement. Point your build system at it:

```bash
# Build a project with make
make CC=/path/to/ccc-x86

# Build a project with CMake
cmake -DCMAKE_C_COMPILER=/path/to/ccc-x86 ..

# Build a project with configure scripts
./configure CC=/path/to/ccc-x86
```

## Usage

```bash
# Compile and link
ccc -o output input.c                # x86-64
ccc-arm -o output input.c            # AArch64
ccc-riscv -o output input.c          # RISC-V 64
ccc-i686 -o output input.c           # i686

# GCC-compatible flags
ccc -S input.c                       # Emit assembly
ccc -c input.c                       # Compile to object file
ccc -E input.c                       # Preprocess only
ccc -O2 -o output input.c            # Optimize (accepts -O0 through -O3, -Os, -Oz)
ccc -g -o output input.c             # DWARF debug info
ccc -DFOO=1 -Iinclude/ input.c       # Define macros, add include paths
ccc -Werror -Wall input.c            # Warning control
ccc -fPIC -shared -o lib.so lib.c    # Position-independent code
ccc -x c -E -                        # Read from stdin

# Build system integration (reports as GCC 14.2.0 for compatibility)
ccc -dumpmachine     # x86_64-linux-gnu / aarch64-linux-gnu / riscv64-linux-gnu / i686-linux-gnu
ccc -dumpversion     # 14
```

The compiler accepts most GCC flags. Unrecognized flags (e.g., architecture-
specific `-m` flags, unknown `-f` flags) are silently ignored so `ccc` can
serve as a drop-in GCC replacement in build systems.

### Assembler and Linker Modes

By default, the compiler uses its **builtin assembler and linker** for all
four architectures. No external toolchain is required. You can verify this
with `--version`, which shows `Backend: standalone` when using the builtin
tools.

To build with optional GCC fallback support (e.g., for debugging), enable
Cargo features at compile time:

```bash
# Build with GCC assembler and linker fallback
cargo build --release --features gcc_assembler,gcc_linker

# Build with GCC fallback for -m16 boot code only
cargo build --release --features gcc_m16
```

| Feature | Description |
|---------|-------------|
| `gcc_assembler` | Use GCC as the assembler instead of the builtin |
| `gcc_linker` | Use GCC as the linker instead of the builtin |
| `gcc_m16` | Use GCC for `-m16` (16-bit real mode boot code) |

When compiled with GCC fallback features enabled, `--version` shows which
components use GCC (e.g., `Backend: gcc_assembler, gcc_linker`).

## Status

The compiler can build real-world C codebases across all four architectures,
including the Linux kernel. Projects that compile and pass their test suites
include PostgreSQL (all 237 regression tests), SQLite, QuickJS, zlib, Lua,
libsodium, libpng, jq, libjpeg-turbo, mbedTLS, libuv, Redis, libffi, musl,
TCC, and DOOM — all using the fully standalone assembler and linker with no
external toolchain. Over 150 additional projects have also been built
successfully, including FFmpeg (all 7331 FATE checkasm tests on x86-64 and
AArch64), GNU coreutils, Busybox, CPython, QEMU, and LuaJIT.

### Known Limitations

- **Long double**: x86 80-bit extended precision is supported via x87 FPU
  instructions. On ARM/RISC-V, `long double` is IEEE binary128 via
  compiler-rt/libgcc soft-float libcalls.
- **Complex numbers**: `_Complex` arithmetic is supported, including
  multiplication and division edge cases per Annex G. Some rare corner cases
  may remain.
- **GNU extensions**: 20 `__attribute__` types are supported (`format`,
  `deprecated`, `warn_unused_result`, `malloc`, `pure`, `const`, `section`,
  `used`, `alias`, `constructor`, `destructor`, `visibility`, `aligned`,
  `packed`, `weak`, `noreturn`, `always_inline`, `noinline`, `cold`, `hot`).
  Unsupported attributes emit a warning via `-Wattributes` (suppressible with
  `-Wno-attributes`). NEON intrinsics cover ≥90% of 128-bit operation families
  including lane manipulation, widening/narrowing, saturating arithmetic,
  load/store variants, comparison, and bitwise operations.

### C11 Conformance and New Features

The compiler implements the following C11 language features and enhancements:

**C11 Language Features:**

- **`_Atomic` qualifier**: Type-system tracking with atomic
  load, store, compound assignment (fetch\_add/sub/and/or/xor), and
  increment/decrement on all four architectures
  (x86-64: LOCK CMPXCHG/XADD/XCHG; AArch64: LDXR/STXR/CASP; RISC-V:
  LR/SC/AMO; i686: LOCK CMPXCHG8B)
- **`_Generic` selection**: Compile-time type matching with exact type, compatible
  type, and `default` association support
- **`_Static_assert`**: Compile-time assertion with improved diagnostics, including
  the single-argument form
- **`_Alignas`**: Alignment specifier with power-of-two validation and minimum
  natural alignment enforcement
- **`_Noreturn`**: Function specifier with reachable-return warning and dead code
  elimination after `_Noreturn` calls
- **Variable Length Arrays (VLAs)**: Stack allocation with runtime `sizeof`
  evaluation, multi-dimensional support, `[*]` parameter syntax, and proper
  scope-based deallocation
- **`restrict` qualifier**: Parsed and propagated through the type system, with
  alias analysis integration in GVN and LICM optimization passes
- **`inline` linkage**: C99 and GNU inline semantics with correct linkage rules

**Tiered Optimization Pipeline:**

Six distinct optimization tiers with per-tier pass configuration:

| Level | Behavior |
|-------|----------|
| `-O0` | Skip all optimization passes and mem2reg |
| `-O1` | Constant folding, copy propagation, DCE, and mem2reg |
| `-O2` | Full optimization pipeline (default) |
| `-O3` | Full pipeline with loop unrolling and aggressive inlining |
| `-Os` | Full pipeline with reduced inlining for smaller binaries |
| `-Oz` | Full pipeline with inlining disabled for minimum size |

**Additional Enhancements:**

- **Tail call optimization**: Self-recursive tail calls optimized on all four
  architectures (previously x86-64 only)
- **Preprocessor**: `#pragma once` with device+inode deduplication, `#pragma pack`
  push/pop/reset stack, `_Pragma("...")` operator desugaring
- **Digraphs and trigraphs**: Digraph tokens recognized unconditionally; trigraph
  processing available via `-trigraphs` flag
- **Parser error recovery**: Multi-error reporting (up to 20 diagnostics per
  translation unit) with source-line caret output
- **Linker script support**: `-T script.ld` with `SECTIONS`, `MEMORY`, `ENTRY`,
  `PROVIDE`, and `KEEP` directives
- **Diagnostics**: `-Werror=<name>` granular warning-to-error promotion,
  `-pedantic` mode for GNU extension warnings
- **NEON intrinsics**: ≥90% coverage of 128-bit NEON operation families
- **Register allocator**: Loop-depth-aware spill weight calculation for improved
  register allocation in hot loops
- **GNU IFUNC**: Indirect function dispatch on x86-64 and AArch64, with
  unsupported-platform diagnostics on i686 and RISC-V
- **Static linking**: `-static` flag with NSS function usage warnings

## Testing

The compiler has two kinds of tests:

**Unit tests** (in-source `#[test]` functions for individual passes and modules):

```bash
cargo test --release
```

**Integration tests** (end-to-end compilation tests in `tests/`). Each test is
a directory containing a `main.c` source file and expected output files:

```
tests/
  some-test-name/
    main.c              # C source to compile
    expected.stdout     # Expected stdout (if any)
    expected.ret        # Expected exit code (if any)
    expected.skip.arm   # Skip marker for specific architectures (optional)
```

Tests are run by compiling `main.c` with `ccc`, executing the resulting binary,
and comparing stdout and the exit code against the expected files.

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `CCC_TIME_PHASES` | Print per-phase compilation timing to stderr |
| `CCC_TIME_PASSES` | Print per-pass optimization timing and change counts to stderr |
| `CCC_DISABLE_PASSES` | Disable specific optimization passes (comma-separated, or `all`) |
| `CCC_KEEP_ASM` | Preserve intermediate `.s` files next to output |
| `CCC_ASM_DEBUG` | Dump preprocessed assembly to `/tmp/asm_debug_<name>.s` |

## Project Organization

```
src/                Compiler source code (Rust)
  frontend/         C source -> typed AST (preprocessor, lexer, parser, sema)
  ir/               Target-independent SSA IR (lowering, mem2reg)
  passes/           SSA optimization passes (16 passes + shared loop analysis)
  backend/          IR -> assembly -> machine code -> ELF (4 architectures)
  common/           Shared types, symbol table, diagnostics
  driver/           CLI parsing, pipeline orchestration

include/            Bundled C headers (x86 SIMD: SSE through AVX-512, AES-NI, FMA, SHA, BMI2; ARM NEON)
tests/              Compiler tests (each test is a directory with main.c and expected output)
docs/               Formal EBNF grammar and specification documents
.github/            CI/CD GitHub Actions workflows
ideas/              Future work proposals and improvement notes
```

Each `src/` subdirectory has its own `README.md` with detailed design
documentation. For the full architecture, compilation pipeline data flow,
and key design decisions, see [DESIGN_DOC.md](DESIGN_DOC.md).
