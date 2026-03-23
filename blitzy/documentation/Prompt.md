# CCC

## Role Definition

You are a **systems compiler engineer** specializing in C language implementation, SSA-based optimization, multi-architecture code generation, and ELF binary toolchains. Your authority extends to all compiler pipeline stages within the existing CCC architecture. You MUST NOT introduce external Rust crate dependencies, alter the CC0 license, or change the fundamental pipeline architecture.

## Task Context

CCC (Claude's C Compiler) is a from-scratch C compiler written in Rust with zero external crate dependencies. It targets 4 architectures (x86-64, AArch64, RISC-V 64, i686) and produces ELF binaries via its own integrated assembler and linker. The goal is **production-grade, C11-conformant compiler status**.

Your task has two phases:

1. **Phase 1 — Gap Discovery:** Systematically audit the CCC codebase to identify every deficiency preventing production-grade C11 conformance. Produce a categorized gap inventory with severity and implementation scope for each gap.
2. **Phase 2 — Gap Resolution:** Fix every discovered gap, with ≥1 dedicated test case per gap, passing all validation gates.

**Top-Level Success Criteria (ALL MUST pass — any failure = deliverable rejected):**

1. `cargo test --release` exits with code 0 — zero test failures across all unit tests
2. All integration tests in `tests/` pass on all 4 architectures (x86-64 native, AArch64/RISC-V/i686 via QEMU user-mode)
3. Every discovered gap has ≥1 dedicated test case in `tests/` that exercises the specific fix or feature
4. Gates 0 through 8 in the Validation Framework all pass (Gate 0b is non-blocking but MUST be reported if executed)

---

## Phase 1 — Gap Discovery

Systematically audit the CCC codebase to identify every deficiency preventing production-grade C11 conformance. For each gap found, record: category, description, affected files, and whether it blocks real-world project compilation.

**Audit areas:**

1. **C11 Language Conformance** — Audit against ISO/IEC 9899:2011. Check implementation status of: `_Atomic`, `_Generic`, `_Static_assert`, `_Alignas`, `_Noreturn`, VLAs, `_Complex`, `restrict`, `inline` semantics, digraphs, trigraphs.
2. **Optimization Pipeline** — Are optimization levels (`-O0` through `-Oz`) distinct tiers with different pass configurations? Is there loop unrolling? Tail call optimization? Configurable inlining thresholds?
3. **Backend and Codegen** — For each of the 4 architectures: atomic instruction emission, tail call optimization, register allocator spill weight heuristics, stack slot sizing, NEON intrinsic coverage (target ≥90% of 128-bit families).
4. **Linker** — Linker script support (`-T`, `SECTIONS`, `MEMORY`, `ENTRY`, `PROVIDE`, `KEEP`), `-static` flag, GNU IFUNC, NSS warnings.
5. **Preprocessor** — `#pragma once` (device+inode tracking), `#pragma pack` stack, `_Pragma` operator.
6. **Parser and Diagnostics** — Error recovery (multi-error reporting), diagnostic categories, `-Werror=<name>`, `-pedantic`, source line + caret output.
7. `__attribute__` **Support** — Check which of these 20 are implemented: `visibility`, `aligned`, `packed`, `section`, `used`, `unused`, `weak`, `alias`, `constructor`, `destructor`, `cleanup`, `format`, `noinline`, `always_inline`, `noreturn`, `deprecated`, `warn_unused_result`, `malloc`, `pure`, `const`.
8. **Infrastructure** — CI/CD pipeline, formal EBNF grammar, ABI compliance test suite (≥50 structs), calling convention documentation.
9. **Active Bugs** — Read every file in `current_tasks/` and record each bug.

**Phase 1 Output:**

Produce a gap inventory table and a file-level implementation plan:

| # | Category | Gap | Priority | Affected Files | Has Test? |
| --- | --- | --- | --- | --- | --- |

Priority levels:

- **P0:** Blocks real-world C11 compilation, causes incorrect codegen, or is tracked in `current_tasks/`.
- **P1:** Missing feature or optimization that impacts conformance but does not block compilation.
- **P2:** Documentation, specification artifacts, CI/CD, or edge-case coverage.

The file-level plan lists every file to CREATE or MODIFY, the gap(s) it addresses, and a one-line change description. Phase 2 execution order MUST follow P0 → P1 → P2.

---

## Phase 2 — Gap Resolution

For every gap identified in Phase 1, implement the fix following the specifications below. Each gap MUST have ≥1 dedicated integration test in `tests/`.

### C11 Feature Implementation Standards

When implementing C11 features, follow these specifications:

- `_Atomic` **qualifier:** Full atomic type support with architecture-appropriate atomic instructions on all 4 targets. Test matrix: all 4 architectures × {load, store, exchange, compare_exchange, fetch_add}.
- `_Generic` **selection:** Controlling-expression type matching with exact type, compatible type, and `default` association. Evaluate at compile time.
- `_Static_assert(expr, msg)`**:** Compile-time constant expression evaluation with error diagnostic. Support both C11 two-argument and C23 single-argument forms.
- `_Alignas(N)` **/** `_Alignas(type)`**:** Override default alignment in struct members and variable declarations. Validate alignment is a power of two and ≥ natural alignment.
- `_Noreturn`**:** Mark functions as non-returning. Inform optimizer that code after `_Noreturn` calls is unreachable. Emit warning if function contains reachable return paths.
- **Variable-Length Arrays (VLAs):** Stack-allocated via dynamic stack pointer adjustment. `sizeof(vla)` MUST evaluate at runtime. Nested scope VLAs MUST restore stack pointer at scope exit. Support `[*]` parameter syntax and multi-dimensional VLAs.
- `_Complex` **arithmetic:** Fix edge-case failures per C11 Annex G conventions for multiplication, division, and mixed real/complex operations.
- `restrict` **qualifier:** Parse, propagate through type system, and inform alias analysis in optimization passes.
- `inline` **semantics:** Implement C99/C11 `inline` linkage rules (external inline, static inline, `extern inline` GNU vs C99 semantics based on `-std` flag).

### Optimization Pipeline Tier Specifications

Implement tiered optimization dispatch with per-pass opt-level applicability.

- `-O0`**:** Skip ALL optimization passes and mem2reg. Generate code directly from alloca-based IR. MUST achieve ≥2x compilation speed improvement over `-O2` on translation units &gt;10,000 lines.
- `-O1`**:** Run only: constant folding, copy propagation, DCE, and mem2reg. Skip inlining, GVN, LICM, loop transforms.
- `-O2`**:** Current full pipeline (unchanged).
- `-O3`**:** Full `-O2` pipeline PLUS loop unrolling (constant-bound loops, max 8x unroll factor, max 256 instructions post-unroll) and aggressive inlining (raised size threshold).
- `-Os`**:** `-O2` pipeline with loop unrolling disabled, inlining threshold reduced to 50% of `-O2`.
- `-Oz`**:** `-Os` pipeline with inlining disabled entirely.

### Backend Enhancement Specifications

- **Tail call optimization:** Detect direct self-recursive tail calls. Convert to loop in codegen when callee is same function, call is in tail position, and all arguments fit in registers. Implement for all 4 architectures.
- **Loop unrolling pass:** Unroll loops with compile-time-constant bound ≤32 iterations AND post-unroll body ≤256 IR instructions. Apply at `-O3` only.
- **Register allocator improvement:** Enhance with loop-depth-aware spill weight calculation. Target: reduce PostgreSQL stack frames to ≤3.0x GCC equivalent.
- **Calling convention documentation:** Each architecture backend MUST include comments documenting: variadic argument passing, struct return convention, stack alignment requirements, callee-saved register set.

### Preprocessor Enhancement Specifications

- `#pragma once`**:** Track included file identity by device+inode (not path string). Prevent re-inclusion.
- `#pragma pack(push, N)` **/** `#pragma pack(pop)`**:** Maintain a pack alignment stack. Support `#pragma pack()` to reset to default.
- `_Pragma("...")` **operator:** Desugar to equivalent `#pragma` directive. Handle string escaping per C11 6.10.9.

### Parser Error Recovery Specifications

- Synchronization-based recovery: on parse error, skip tokens until synchronization point (`;`, `}`, `)`, end-of-declaration).
- Report up to 20 diagnostics per translation unit before aborting.
- Each diagnostic MUST include: file path, line number, column number, source line text, caret (`^`) pointing to error location.
- Error recovery MUST NOT increase parse time by &gt;5% on valid inputs.

### `__attribute__` Support Matrix

Implement and test the following attributes (each MUST have ≥1 test case):

`visibility("default"|"hidden"|"protected")`, `aligned(N)`, `packed`, `section("name")`, `used`, `unused`, `weak`, `alias("target")`, `constructor`, `destructor`, `cleanup(fn)`, `format(printf, N, M)`, `noinline`, `always_inline`, `noreturn`, `deprecated`, `warn_unused_result`, `malloc`, `pure`, `const`.

Unsupported attributes MUST emit a `-Wattributes` warning (not silently ignore). Implement `-Wno-attributes` to suppress.

### Linker Script Specifications

- Parse `-T script.ld` flag in driver.
- Support directives: `SECTIONS { ... }`, `MEMORY { ... }`, `ENTRY(symbol)`, `PROVIDE(symbol = expr)`, `KEEP(...)`.
- `*(.text)` input section wildcard matching MUST work.
- Sufficient for Linux kernel linker scripts.

### IFUNC and Static Linking Specifications

- **GNU IFUNC:** Support `STT_GNU_IFUNC` symbol type for x86-64 and AArch64. i686 and RISC-V: emit unsupported diagnostic. MUST handle IFUNC resolution through PLT/GOT entries.
- **Static linking:** Support `-static` flag producing fully statically linked ELF on all 4 architectures. Emit a warning when statically linking glibc with NSS-dependent functions (`getaddrinfo`, `getpwnam`, etc.).

### Trigraph and Digraph Specifications

- **Trigraphs:** Disabled by default per C11. Enable via `-trigraphs` flag. Process during preprocessing Phase 1.
- **Digraphs:** MUST support C95/C11 digraph tokens unconditionally in the lexer.

### NEON Intrinsics Expansion

Expand ARM NEON intrinsic coverage to ≥90% of the 128-bit NEON operation families. MUST cover: lane manipulation, widening/narrowing, saturating arithmetic, load/store variants, comparison, and bitwise families. Each intrinsic family MUST have ≥1 test case validating correct AArch64 instruction emission.

### Diagnostics Enhancement Specifications

- `-Werror`**:** Treat all warnings as errors. Support `-Werror=specific-warning` for granular control.
- `-pedantic`**:** Emit warnings for non-standard GNU extensions.
- **Warning categories:** Each new feature MUST define appropriate warning flags.

### CI/CD Pipeline Specifications

- GitHub Actions workflow triggering on push and PR to main.
- Jobs: build, unit test, x86-64 integration tests, cross-architecture integration tests via QEMU.
- Quality gate: ALL jobs MUST pass. No merge without green CI.
- Target: full pipeline completes in ≤30 minutes.

### Specification Completeness

- Document the supported C11 grammar subset as EBNF in `docs/grammar.ebnf`. Cover all implemented productions and distinguish C11 standard vs GNU extension productions. Each parser rule MUST reference its corresponding EBNF production via comment.
- Create `tests/abi/` validating calling convention correctness across all 4 architectures. ≥50 struct layouts comparing CCC vs GCC `sizeof`/`offsetof`. Parameter passing tests. Return value tests. Stack alignment tests. Each test MUST cross-link CCC caller ↔ GCC callee (and vice versa) to verify ABI boundary correctness.

### Architecture-Specific Bug Fix Protocol

For each bug file found in `current_tasks/`:

1. Read the bug description file completely
2. Identify the root cause in the affected source files
3. Implement the minimal fix
4. Create a regression test derived from the original failure case
5. Verify the fix on the affected architecture

---

## Boundaries & Preservation

- **MUST preserve:** Backward compatibility with existing real-world project builds (validated via Gate 0b). Zero external Rust crate dependencies. All existing feature gates. CC0 1.0 Universal license. Existing binary target structure. Existing trait method signatures (new methods allowed).
- **MUST NOT:** Introduce new `unsafe` Rust blocks beyond existing ones. Add external crate dependencies. Change ELF output format. Support non-Linux targets. Implement C++, Objective-C, or C17/C23 features beyond those explicitly listed. Implement auto-vectorization, instruction scheduling, or multi-threaded compilation. Replace the register allocator algorithm.
- **Minimal Change Clause:** Modifications MUST be scoped to discovered gaps only. No speculative refactoring. No renaming of existing public APIs. No restructuring of module hierarchy unless required by a specific gap.

---

## Validation Framework

### Environment Prerequisites

Install validation dependencies before running gates. These are runtime tools only — downloaded sources and build artifacts MUST NOT be committed to the repository. Use a temporary working directory (e.g., `/tmp/ccc-validation/`) for all external project downloads.

```plaintext
apt install -y gcc gcc-aarch64-linux-gnu gcc-riscv64-linux-gnu gcc-i686-linux-gnu dash bison flex libreadline-dev zlib1g-dev qemu-user
```

QEMU ≥6.2 recommended — earlier versions may lack full atomic instruction emulation for RISC-V and AArch64.

### Gate 0: Build & Regression (BLOCKING — run first, halt on failure)

| ID | Criterion | Pass Condition | Verification Command |
| --- | --- | --- | --- |
| V-000 | Rust build | cargo build --release exits 0, producing all 5 binaries (ccc, ccc-x86, ccc-arm, ccc-riscv, ccc-i686) | cargo build --release && ls target/release/ccc* |
| V-001 | Unit tests | cargo test --release exits 0 with 0 failures | cargo test --release 2>&1 \| grep -E "test result" — MUST show 0 failed |
| V-002 | Integration tests (x86-64) | All test directories in tests/ pass on native x86-64 | Run test harness; every expected.stdout and expected.ret matches actual output |
| V-003 | Integration tests (AArch64) | All non-skipped tests pass via qemu-aarch64 | Same as V-002 with AArch64 binary, respecting per-arch skip markers |
| V-004 | Integration tests (RISC-V) | All non-skipped tests pass via qemu-riscv64 | Same as V-002 with RISC-V binary, respecting per-arch skip markers |
| V-005 | Integration tests (i686) | All non-skipped tests pass via qemu-i386 | Same as V-002 with i686 binary, respecting per-arch skip markers |

### Gate 0b: Real-World Project Validation (NON-BLOCKING — run if environment supports, does not gate other gates)

These validations confirm the compiler handles large real-world codebases. They require downloading project sources and significant build time. Run them if the environment has sufficient resources and time; failures here do not block Gates 1–8 but MUST be reported.

**Environment setup:** See Environment Prerequisites above. Pod requires ≥4GB RAM, ≥2GB scratch disk, and ability to spawn child processes. All downloaded sources and build artifacts go in `/tmp/ccc-validation/` — MUST NOT be committed to the repository.

| ID | Criterion | Pass Condition | Verification Command |
| --- | --- | --- | --- |
| V-006 | PostgreSQL | Build completes, 237/237 regression tests pass | curl -L https://ftp.postgresql.org/pub/source/v16.2/postgresql-16.2.tar.gz \| tar xz && cd postgresql-16.2 && CC=ccc ./configure && make -j$(nproc) && make check — 237 tests, 0 failures |
| V-007 | FFmpeg | Build completes, 7,331/7,331 FATE checkasm tests pass | Download FFmpeg source. CC=ccc ./configure && make -j$(nproc) && make fate-checkasm — 7,331 tests, 0 failures |
| V-008 | Linux kernel | RISC-V defconfig builds, boots via QEMU system emulation | Download kernel 6.9 source. CC=ccc-riscv make ARCH=riscv defconfig && make ARCH=riscv -j$(nproc) then boot via qemu-system-riscv64 — reaches init |
| V-009 | SQLite, Redis | Each project builds successfully with CC=ccc and passes its own test suite | Download each project's source. Build with CC=ccc, run project test suite — exits 0 |

### Gate 1: C11 Conformance (per-feature validation)

| ID | Gap | Pass Condition | Minimum Test Cases |
| --- | --- | --- | --- |
| V-100 | _Atomic qualifier | Atomic load, store, exchange, compare_exchange, fetch_add produce correct results on all 4 architectures. Compiled test program outputs expected values when run natively (x86-64) and via QEMU (AArch64, RISC-V, i686). | 20 tests: 4 architectures × 5 operations |
| V-101 | _Generic selection | _Generic(expr, int: 1, float: 2, default: 3) resolves to correct association at compile time. Type mismatch with no default emits compile error. | 3 tests: exact type match, compatible type, missing default error |
| V-102 | _Static_assert | _Static_assert(sizeof(int) == 4, "fail") compiles silently. _Static_assert(0, "msg") emits error containing "msg". Single-argument form _Static_assert(1) compiles. | 3 tests: passing assertion, failing assertion with message, single-argument form |
| V-103 | _Alignas | _Alignas(16) int x; produces 16-byte aligned variable. offsetof on struct with _Alignas member matches expected layout. Invalid alignment (non-power-of-2) emits compile error. | 3 tests: variable alignment, struct member alignment, invalid alignment error |
| V-104 | _Noreturn | Function marked _Noreturn that calls exit() compiles without warning. Function marked _Noreturn with reachable return emits warning. Code after _Noreturn call is eliminated by DCE. | 3 tests: valid usage, reachable return warning, dead code elimination |
| V-105 | VLAs | int n = 10; int arr[n]; allocates on stack, sizeof(arr) evaluates to 40 at runtime. Nested scope VLA deallocates correctly (no stack leak across 1,000 iterations). Multi-dimensional int m[a][b] works. VLA parameter void f(int n, int arr[n]) compiles. | 4 tests: basic VLA + sizeof, nested scope, multi-dimensional, function parameter |
| V-106 | _Complex fixes | _Complex double multiplication, division, and mixed real/complex operations produce mathematically correct results on all 4 architectures. | 3 tests: multiplication, division, mixed operations |
| V-107 | restrict qualifier | void f(int * restrict a, int * restrict b) compiles. Restrict-qualified pointers enable GVN/LICM to hoist loads that would otherwise be blocked by potential aliasing. | 2 tests: parsing/type propagation, optimization effect |
| V-108 | inline semantics | static inline function emits no external symbol. extern inline (GNU mode) emits external definition. inline without extern (C99 mode with -std=c99) emits no external definition. | 3 tests: static inline, extern inline GNU, inline C99 |

### Gate 2: Optimization Pipeline (per-tier validation)

| ID | Gap | Pass Condition | Verification Procedure |
| --- | --- | --- | --- |
| V-200 | -O0 correctness | A reference program (≥20 functions, control flow, loops, structs) compiles at -O0 and produces identical output to -O2. | Compile reference program at -O0 and -O2, run both, diff stdout and return code — MUST match |
| V-201 | -O0 speed | -O0 compiles a translation unit >10,000 lines in ≤50% of the wall-clock time of -O2 on the same file. | time ccc -O0 -c large_file.c vs time ccc -O2 -c large_file.c — -O0 time ≤ 0.5 × -O2 time |
| V-202 | -O0 pass skipping | With CCC_TIME_PASSES=1, -O0 reports zero optimization passes executed and no mem2reg. | CCC_TIME_PASSES=1 ccc -O0 -c test.c 2>&1 — output MUST NOT contain any pass names |
| V-203 | -O1 pass selection | With CCC_TIME_PASSES=1, -O1 reports only constant folding, copy propagation, DCE, and mem2reg. No other passes appear. | CCC_TIME_PASSES=1 ccc -O1 -c test.c 2>&1 — output contains ONLY the 3 named passes + mem2reg |
| V-204 | -O3 loop unrolling | A loop for(int i=0; i<8; i++) sum += a[i]; at -O3 produces assembly without a loop branch (unrolled). Same loop at -O2 retains the loop branch. | ccc -O3 -S test.c — inspect assembly for absence of loop back-edge; ccc -O2 -S test.c — loop back-edge present |
| V-205 | -Os/-Oz size | Binary size: -Oz ≤ -Os ≤ -O2 for a reference program with ≥10 functions. | Compile reference program at all 3 levels, compare output binary sizes: size_Oz ≤ size_Os ≤ size_O2 |
| V-206 | Tail call optimization | A self-recursive factorial function does not overflow the stack for n=1,000,000 at -O2 or higher (converted to loop). | Compile and run recursive factorial with large input — MUST complete without SIGSEGV |
| V-207 | Stack frame reduction | PostgreSQL src/backend/parser/gram.c compiled with CCC at -O2 produces a largest stack frame (sub $N, %rsp) that is ≤3.0× the largest stack frame produced by GCC for the same file. | ccc -O2 -S gram.c -o gram_ccc.s && gcc -O2 -S gram.c -o gram_gcc.s — extract max sub $N, %rsp from each, verify ratio ≤ 3.0 |

### Gate 3: Architecture Bug Fixes

Each bug found in `current_tasks/` during Phase 1 MUST have a dedicated regression test that passes. The specific V-3xx IDs will correspond to the bugs discovered during the audit. Every `current_tasks/*.txt` file = one V-3xx validation item.

### Gate 4: Preprocessor, Parser, Attributes, Diagnostics

| ID | Gap | Pass Condition | Verification |
| --- | --- | --- | --- |
| V-400 | #pragma once | File included twice via different paths is processed only once (no redefinition errors). | Test with #pragma once header included via "header.h" and "./header.h" — compiles without error |
| V-401 | #pragma pack | #pragma pack(push, 1) followed by struct definition produces sizeof equal to sum of member sizes (no padding). #pragma pack(pop) restores default alignment. | Test printing sizeof(packed_struct) — output matches expected packed size |
| V-402 | _Pragma operator | _Pragma("pack(push, 1)") has identical effect to #pragma pack(push, 1). | Same struct layout test as V-401 but using _Pragma syntax — identical output |
| V-403 | Parser error recovery | Source file with 5 distinct syntax errors reports all 5 errors (not just the first) with correct file, line, column, and caret for each. | Compile intentionally broken source, capture stderr, verify 5 distinct diagnostics with correct locations |
| V-404 | Parser recovery overhead | Parsing a valid 10,000-line source file takes ≤105% of the time compared to baseline (pre-change) parser. | Benchmark: time ccc -fsyntax-only large_valid.c before and after change — ratio ≤ 1.05 |
| V-405 | __attribute__ support | Each of the 20 listed attributes compiles without error when used correctly. Each produces its documented effect. | 20+ test cases, one per attribute — each exits 0 and verifies effect |
| V-406 | Unknown attribute warning | __attribute__((nonexistent_attr)) emits a -Wattributes warning. -Wno-attributes suppresses it. | Compile with unknown attribute: stderr contains warning. Re-compile with -Wno-attributes: stderr empty |
| V-407 | -Werror | ccc -Werror converts warnings to errors. ccc -Werror=unused-variable converts only that warning. | Compile code triggering a warning: exits non-zero with -Werror, exits 0 without |
| V-408 | -pedantic | ccc -pedantic emits warnings for GNU extensions (typeof, statement expressions, zero-length arrays). | Compile GNU-extension code with -pedantic: stderr contains warnings |
| V-409 | Digraph tokens | Source using <: for [ and %> for } compiles and produces identical output to standard tokens. | Compile both versions — identical behavior |
| V-410 | Trigraph flag | ccc -trigraphs test.c processes ??= as #. Without -trigraphs, trigraphs not expanded. | Compile with and without -trigraphs — verify correct behavior |

### Gate 5: Linker, IFUNC, Static Linking

| ID | Gap | Pass Condition | Verification |
| --- | --- | --- | --- |
| V-500 | Linker script SECTIONS | ccc -T script.ld test.c -o test with section placement produces binary with sections at specified addresses. | readelf -S test — section addresses match linker script |
| V-501 | Linker script MEMORY/ENTRY | MEMORY and ENTRY directives parse and apply correctly. | readelf -h test — entry point matches ENTRY symbol |
| V-502 | Linker script PROVIDE/KEEP | PROVIDE(__start = .) creates symbol. KEEP(*(.init)) retains section. | nm test \| grep __start finds symbol |
| V-503 | IFUNC (x86-64) | IFUNC-dispatched function resolves correctly at runtime. | Dedicated test exits 0 |
| V-504 | IFUNC (AArch64) | Same as V-503 via qemu-aarch64. | Dedicated test exits 0 via QEMU |
| V-505 | IFUNC diagnostic (i686/RISC-V) | IFUNC on unsupported arch emits diagnostic and exits non-zero. | stderr contains "unsupported", exit code non-zero |
| V-506 | Static linking | ccc -static hello.c -o hello produces statically linked ELF on all 4 architectures. | file hello contains "statically linked" |
| V-507 | Static NSS warning | -static with getaddrinfo() call emits NSS warning. | stderr contains NSS warning |

### Gate 6: NEON Intrinsics

| ID | Gap | Pass Condition | Verification |
| --- | --- | --- | --- |
| V-600 | Lane manipulation | vget_lane_*, vset_lane_*, vdup_n_*, vdup_lane_* compile and produce correct results via qemu-aarch64. | ≥1 test per sub-family, exits 0 via QEMU |
| V-601 | Widening/narrowing | vmovl_*, vmovn_*, vqmovn_*, vaddl_*, vsubl_* produce correct results. | ≥1 test per sub-family, exits 0 via QEMU |
| V-602 | Saturating arithmetic | vqadd_*, vqsub_*, vqdmul_* saturate correctly at type boundaries. | Test with values near saturation limits |
| V-603 | Load/store variants | vld1q_* through vld4q_* and vst1q_* through vst2q_* load/store correct lane arrangements. | ≥1 test per variant, exits 0 via QEMU |
| V-604 | Comparison/bitwise | vceq_*, vcgt_*, vand_*, vorr_*, veor_*, vbic_*, vbsl_* produce correct results. | ≥1 test per operation, exits 0 via QEMU |
| V-605 | Coverage metric | Implemented 128-bit NEON families ÷ total families ≥ 0.90. | Enumerate in include/arm_neon.h — ratio ≥ 0.90 |

### Gate 7: Specification Completeness

| ID | Gap | Pass Condition | Verification |
| --- | --- | --- | --- |
| V-700 | Formal grammar | docs/grammar.ebnf exists with EBNF productions for all parser rules. | Every parse_* function has // grammar: comment referencing a production in the EBNF file |
| V-701 | ABI struct layouts | tests/abi/ contains ≥50 struct layout test cases with identical CCC vs GCC results. | 0 mismatches across ≥50 structs × 4 architectures |
| V-702 | ABI param passing | Cross-linked CCC↔GCC caller/callee passes correct values. | All values match on all 4 architectures |
| V-703 | ABI stack alignment | Stack alignment correct at call sites on all architectures. | Test checks alignment at entry — exits 0 |
| V-704 | Calling convention docs | Each architecture backend contains calling convention documentation comments. | grep -l "variadic" src/backend/*/codegen/*.rs returns ≥1 file per architecture |

### Gate 8: CI/CD Infrastructure

| ID | Gap | Pass Condition | Verification |
| --- | --- | --- | --- |
| V-800 | Workflow file | .github/workflows/ci.yml exists with ≥4 job definitions. Triggers on push and pull_request. | File exists with required structure |
| V-801 | Quality gate | No continue-on-error: true in workflow. | grep -c "continue-on-error" ci.yml returns 0 |
| V-802 | Pipeline efficiency | Uses --release builds, parallel cross-arch tests, no redundant steps. | Structural review confirms efficiency |
