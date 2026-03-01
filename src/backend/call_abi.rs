//! Unified ABI classification for both call-site arguments and callee-side parameters.
//!
//! The core classification algorithm (struct layout, register assignment, stack overflow)
//! is the same for callers and callees — they must agree on where each argument lives.
//! Previously this logic was duplicated in two separate files with parallel enum
//! hierarchies (`CallArgClass` and `ParamClass`). This module unifies them:
//!
//! - `CallArgClass`: caller-side classification (no stack offsets needed)
//! - `ParamClass`: callee-side classification (tracks stack offsets for loading params)
//! - `classify_args_core`: single implementation of the classification algorithm
//! - `classify_call_args` / `classify_params_full`: thin wrappers over the core
//!
//! # Architecture-Specific Calling Conventions
//!
//! This module handles four distinct calling conventions through a unified
//! [`CallAbiConfig`] struct. Below is a reference summary of each ABI.
//!
//! ## x86-64 — SysV AMD64 ABI
//!
//! | Property              | Detail                                                      |
//! |-----------------------|-------------------------------------------------------------|
//! | GP argument registers | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` (6 registers)       |
//! | FP argument registers | `xmm0`–`xmm7` (8 registers)                                |
//! | Integer return        | `rax` (64-bit), `rdx:rax` (128-bit)                         |
//! | Float return          | `xmm0` (scalar float/double), `xmm0:xmm1` (struct SSE)     |
//! | Struct return (sret)  | Hidden first argument in `rdi`; consumes a GP register slot |
//! | Variadic convention   | Float count passed in `al`; float args in XMM registers     |
//! | Stack alignment       | 16-byte aligned before `call` instruction                   |
//! | Callee-saved regs     | `rbx`, `rbp`, `r12`–`r15`                                  |
//! | Red zone              | 128 bytes below `rsp` (disabled with `-mno-red-zone`)       |
//!
//! The SysV AMD64 ABI classifies struct fields per-eightbyte into INTEGER or SSE
//! classes, allowing small structs to be passed in a mix of GP and XMM registers.
//! This is controlled by [`CallAbiConfig::use_sysv_struct_classification`].
//!
//! ## AArch64 — AAPCS64 (ARM 64-bit Procedure Call Standard)
//!
//! | Property              | Detail                                                            |
//! |-----------------------|-------------------------------------------------------------------|
//! | GP argument registers | `x0`–`x7` (8 registers)                                          |
//! | FP argument registers | `v0`–`v7` / `d0`–`d7` (8 registers)                              |
//! | Integer return        | `x0` (64-bit), `x0:x1` (128-bit)                                 |
//! | Float return          | `d0` (scalar), `v0` (NEON vector)                                 |
//! | Struct return (sret)  | Dedicated `x8` register (indirect result location); does NOT      |
//! |                       | consume a GP argument slot — see [`CallAbiConfig::sret_uses_dedicated_reg`] |
//! | Variadic convention   | Float args promoted to GP registers after the last named param    |
//! | Stack alignment       | 16-byte aligned at public interfaces                              |
//! | Callee-saved regs     | `x19`–`x28`, `x29` (frame pointer), `x30` (link register)        |
//! | Large structs (>16B)  | Passed by reference: pointer in GP register or on stack           |
//!
//! AArch64 does not use per-eightbyte classification like x86-64. Small structs
//! (≤16 bytes) are passed in 1–2 GP registers as opaque blobs. Large structs
//! (>16 bytes) are passed by reference (pointer in a GP register), controlled by
//! [`CallAbiConfig::large_struct_by_ref`].
//!
//! ## RISC-V 64 — LP64D ABI (Double-Float Calling Convention)
//!
//! | Property              | Detail                                                                  |
//! |-----------------------|-------------------------------------------------------------------------|
//! | GP argument registers | `a0`–`a7` (8 registers)                                                |
//! | FP argument registers | `fa0`–`fa7` (8 registers)                                              |
//! | Integer return        | `a0` (64-bit), `a0:a1` (128-bit)                                       |
//! | Float return          | `fa0` (scalar float/double)                                             |
//! | Struct return (sret)  | Hidden first argument in `a0`; consumes a GP register slot              |
//! | Variadic convention   | Float args promoted to GP registers for variadic calls                  |
//! | Stack alignment       | 16-byte aligned                                                         |
//! | Callee-saved regs     | `s0` (frame pointer), `s1`, `s2`–`s11`, `ra` (return address)           |
//! | Struct splitting      | 2×XLEN structs may split across last GP reg and stack                   |
//! | F128 (long double)    | Passed in GP register pair (`a0:a1`-style), not FP registers            |
//! | Even-align pairs      | 2×XLEN-aligned composites require even-aligned register pairs           |
//!
//! RISC-V LP64D uses hardware floating-point struct classification: small structs
//! containing only float/double fields can be passed in FP registers. This is
//! controlled by [`CallAbiConfig::use_riscv_float_struct_classification`].
//! A 2-register struct may be split across the last GP register and the stack
//! when [`CallAbiConfig::allow_struct_split_reg_stack`] is set.
//!
//! ## i686 — cdecl (ILP32, IA-32 System V ABI)
//!
//! | Property              | Detail                                                        |
//! |-----------------------|---------------------------------------------------------------|
//! | GP argument registers | 0 by default (all arguments on stack); up to 3 with           |
//! |                       | `-mregparm=N` (`eax`, `edx`, `ecx`)                          |
//! | FP argument registers | None (x87 floating-point stack)                               |
//! | Integer return        | `eax` (32-bit), `edx:eax` (64-bit)                            |
//! | Float return          | `st(0)` (x87 top-of-stack for float, double, long double)     |
//! | Struct return (sret)  | Hidden first argument on stack; callee pops the sret pointer  |
//! | Variadic convention   | All arguments on stack; no register promotion                 |
//! | Stack alignment       | 4-byte minimum; 16-byte for SSE operations                    |
//! | Callee-saved regs     | `ebx`, `esi`, `edi`, `ebp`                                   |
//! | Long double (F128)    | 12 bytes (80-bit extended padded to 12), 4-byte aligned       |
//!
//! On i686, the default cdecl convention passes all arguments on the stack
//! (right-to-left push order). The `-mregparm=N` extension allows up to 3
//! integer arguments in GP registers. Floating-point values always use the
//! x87 FPU stack for passing and returning.

use crate::ir::reexports::{IrConst, IrFunction, Operand};
use crate::common::types::IrType;
use super::generation::is_i128_type;

// ---------------------------------------------------------------------------
// CallArgClass — caller-side classification (used by emit_call_*)
// ---------------------------------------------------------------------------

/// Classification of a function call argument for register/stack assignment.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CallArgClass {
    /// Integer/pointer argument in a GP register. `reg_idx` is the GP register index.
    IntReg { reg_idx: usize },
    /// Float argument in an FP register. `reg_idx` is the FP register index.
    FloatReg { reg_idx: usize },
    /// 128-bit integer in a GP register pair. `base_reg_idx` is the first register.
    I128RegPair { base_reg_idx: usize },
    /// F128 (long double) — handling is arch-specific (x87 on x86, Q-reg on ARM, GP pair on RISC-V).
    F128Reg { reg_idx: usize },
    /// Small struct (<=16 bytes) passed by value in 1-2 GP registers.
    StructByValReg { base_reg_idx: usize, size: usize },
    /// Small struct (<=16 bytes) where all fields are float/double (SSE class per SysV ABI).
    /// Passed in 1-2 XMM registers instead of GP registers.
    /// `lo_fp_idx` is the FP register for the first eightbyte, `hi_fp_idx` for the second (if size > 8).
    StructSseReg { lo_fp_idx: usize, hi_fp_idx: Option<usize>, size: usize },
    /// Small struct where first eightbyte is INTEGER and second is SSE (mixed).
    StructMixedIntSseReg { int_reg_idx: usize, fp_reg_idx: usize, size: usize },
    /// Small struct where first eightbyte is SSE and second is INTEGER (mixed).
    StructMixedSseIntReg { fp_reg_idx: usize, int_reg_idx: usize, size: usize },
    /// Small struct (<=16 bytes) that overflows to the stack.
    StructByValStack { size: usize },
    /// Small struct split across the last GP register and the stack.
    /// RISC-V psABI: first XLEN bytes in `reg_idx`, remaining bytes on the stack.
    StructSplitRegStack { reg_idx: usize, size: usize },
    /// Large struct (>16 bytes) passed on the stack (MEMORY class).
    LargeStructStack { size: usize },
    /// Argument overflows to the stack (normal 8-byte).
    Stack,
    /// F128 argument overflows to the stack (16-byte aligned).
    F128Stack,
    /// I128 argument overflows to the stack (16-byte aligned).
    I128Stack,
    /// Zero-size struct argument (e.g., `struct { char x[0]; }`).
    /// Per GCC behavior, zero-size struct arguments consume no register or stack space.
    ZeroSizeSkip,
}

impl CallArgClass {
    /// Returns true if this argument is passed on the stack (any kind).
    pub fn is_stack(&self) -> bool {
        matches!(self, CallArgClass::Stack | CallArgClass::F128Stack |
                 CallArgClass::I128Stack | CallArgClass::StructByValStack { .. } |
                 CallArgClass::LargeStructStack { .. } |
                 CallArgClass::StructSplitRegStack { .. })
    }

    /// Returns the stack space consumed by this argument (0 if register).
    pub fn stack_bytes(&self) -> usize {
        let slot_size = crate::common::types::target_ptr_size(); // 8 for LP64, 4 for ILP32
        let align_mask = slot_size - 1;
        match self {
            CallArgClass::F128Stack => if slot_size == 4 { 12 } else { 16 }, // i686: x87 long double = 12 bytes
            CallArgClass::I128Stack => 16,
            CallArgClass::StructByValStack { size } | CallArgClass::LargeStructStack { size } => {
                (*size + align_mask) & !align_mask
            }
            CallArgClass::StructSplitRegStack { size, .. } => {
                // Only the portion beyond the first register goes on the stack.
                let stack_part = size - slot_size;
                (stack_part + align_mask) & !align_mask
            }
            CallArgClass::Stack => slot_size,
            _ => 0,
        }
    }
}

// ---------------------------------------------------------------------------
// ParamClass — callee-side classification (used by emit_store_params)
// ---------------------------------------------------------------------------

/// Classification of a function parameter for `emit_store_params`.
///
/// Each variant tells the backend exactly where the parameter arrives and what
/// kind of store logic is needed, without the backend reimplementing the ABI
/// classification algorithm.
#[derive(Debug, Clone, Copy)]
pub enum ParamClass {
    /// Integer/pointer in GP register at `reg_idx`.
    IntReg { reg_idx: usize },
    /// Float/double in FP register at `reg_idx`.
    FloatReg { reg_idx: usize },
    /// i128 in aligned GP register pair starting at `base_reg_idx`.
    I128RegPair { base_reg_idx: usize },
    /// Small struct (<=16 bytes) by value in 1-2 GP registers.
    StructByValReg { base_reg_idx: usize, size: usize },
    /// Small struct where all eightbytes are SSE class -> 1-2 XMM registers.
    StructSseReg { lo_fp_idx: usize, hi_fp_idx: Option<usize>, size: usize },
    /// Small struct: first eightbyte INTEGER, second SSE.
    /// (`size` mirrors `CallArgClass`/`CoreArgClass` for structural consistency.)
    #[allow(dead_code)] // size field not yet read by any backend
    StructMixedIntSseReg { int_reg_idx: usize, fp_reg_idx: usize, size: usize },
    /// Small struct: first eightbyte SSE, second INTEGER.
    /// (`size` mirrors `CallArgClass`/`CoreArgClass` for structural consistency.)
    #[allow(dead_code)] // size field not yet read by any backend
    StructMixedSseIntReg { fp_reg_idx: usize, int_reg_idx: usize, size: usize },
    /// F128 (long double) in FP register (ARM: Q-reg).
    F128FpReg { reg_idx: usize },
    /// F128 in GP register pair (RISC-V).
    F128GpPair { lo_reg_idx: usize, hi_reg_idx: usize },
    /// F128 always on stack (x86: x87 convention).
    F128AlwaysStack { offset: i64 },
    /// Regular scalar on the stack.
    StackScalar { offset: i64 },
    /// i128 on the stack (16-byte aligned).
    I128Stack { offset: i64 },
    /// F128 on the stack (overflow from registers).
    F128Stack { offset: i64 },
    /// Small struct that overflowed to the stack.
    StructStack { offset: i64, size: usize },
    /// Small struct split across the last GP register and the stack.
    /// RISC-V psABI: first XLEN bytes in `reg_idx`, remaining bytes at `stack_offset`.
    StructSplitRegStack { reg_idx: usize, stack_offset: i64, size: usize },
    /// Large struct (>16 bytes) passed on the stack.
    LargeStructStack { offset: i64, size: usize },
    /// Large struct (>16 bytes) passed by reference in a GP register (AAPCS64).
    /// The register holds a pointer to the struct data; callee must copy from it.
    LargeStructByRefReg { reg_idx: usize, size: usize },
    /// Large struct (>16 bytes) passed by reference on the stack (AAPCS64, overflow case).
    /// The stack slot holds a pointer to the struct data; callee must copy from it.
    LargeStructByRefStack { offset: i64, size: usize },
    /// Zero-size struct parameter (e.g., `struct { char x[0]; }`).
    /// Per GCC behavior, zero-size struct parameters consume no register or stack space.
    ZeroSizeSkip,
}

impl ParamClass {
    /// Returns true if this parameter is passed on the stack (fully or partially).
    pub fn is_stack(&self) -> bool {
        matches!(self,
            ParamClass::StackScalar { .. } | ParamClass::I128Stack { .. } |
            ParamClass::F128Stack { .. } | ParamClass::F128AlwaysStack { .. } |
            ParamClass::StructStack { .. } | ParamClass::LargeStructStack { .. } |
            ParamClass::LargeStructByRefStack { .. } |
            ParamClass::StructSplitRegStack { .. }
        )
    }

    /// Returns true if this parameter arrives in a GP register (int, i128 pair, struct, or F128 GP pair).
    pub fn uses_gp_reg(&self) -> bool {
        matches!(self,
            ParamClass::IntReg { .. } | ParamClass::I128RegPair { .. } |
            ParamClass::StructByValReg { .. } | ParamClass::F128GpPair { .. } |
            ParamClass::LargeStructByRefReg { .. } |
            ParamClass::StructMixedIntSseReg { .. } | ParamClass::StructMixedSseIntReg { .. } |
            ParamClass::StructSplitRegStack { .. }
        )
    }

    /// Returns the number of stack bytes consumed by this parameter classification.
    /// Used by variadic function handling to compute how many stack bytes named
    /// parameters occupy, so va_start can skip past them.
    pub fn stack_bytes(&self) -> usize {
        let slot_size = crate::common::types::target_ptr_size(); // 8 for LP64, 4 for ILP32
        let align_mask = slot_size - 1;
        match self {
            ParamClass::StackScalar { .. } => slot_size,
            ParamClass::I128Stack { .. } => 16,
            ParamClass::F128Stack { .. } => 16,
            ParamClass::F128AlwaysStack { .. } => if slot_size == 4 { 12 } else { 16 },
            ParamClass::StructStack { size, .. } => (*size + align_mask) & !align_mask,
            ParamClass::StructSplitRegStack { size, .. } => {
                // Only the portion beyond the first register goes on the stack.
                let stack_part = size - slot_size;
                (stack_part + align_mask) & !align_mask
            }
            ParamClass::LargeStructStack { size, .. } => (*size + align_mask) & !align_mask,
            ParamClass::LargeStructByRefStack { .. } => slot_size, // pointer on stack
            _ => 0, // register-passed params don't consume stack space
        }
    }

    /// Returns the number of GP registers consumed by this parameter classification.
    /// Used by variadic function handling to compute the correct va_start offset.
    pub fn gp_reg_count(&self) -> usize {
        match self {
            ParamClass::IntReg { .. } => 1,
            ParamClass::LargeStructByRefReg { .. } => 1, // pointer in one GP reg
            ParamClass::I128RegPair { .. } => 2,
            ParamClass::StructByValReg { size, .. } => {
                // 1 reg for <=8 bytes, 2 regs for >8 bytes (up to 16)
                if *size <= 8 { 1 } else { 2 }
            }
            ParamClass::F128GpPair { .. } => 2,
            ParamClass::StructMixedIntSseReg { .. } | ParamClass::StructMixedSseIntReg { .. } => 1,
            ParamClass::StructSplitRegStack { .. } => 1, // only 1 GP reg used (the rest on stack)
            ParamClass::StructSseReg { .. } => 0, // all SSE, no GP regs
            _ => 0, // FP regs and stack don't consume GP regs
        }
    }
}

// ---------------------------------------------------------------------------
// ABI configuration and SysV struct classification (unchanged)
// ---------------------------------------------------------------------------

/// ABI configuration for call argument classification.
///
/// Each field in this struct captures a specific dimension of ABI behavior that
/// varies across the four supported architectures. The per-architecture configurations
/// are constructed by each backend's codegen module and passed to
/// [`classify_call_args`] (caller side) and [`classify_params_full`] (callee side).
///
/// ## Typical Per-Architecture Values
///
/// | Field                                   | x86-64 | AArch64 | RISC-V 64 | i686  |
/// |-----------------------------------------|--------|---------|-----------|-------|
/// | `max_int_regs`                          | 6      | 8       | 8         | 0–3*  |
/// | `max_float_regs`                        | 8      | 8       | 8         | 0     |
/// | `align_i128_pairs`                      | false  | true    | true      | false |
/// | `f128_in_fp_regs`                       | false  | true    | false     | false |
/// | `f128_in_gp_pairs`                      | false  | false   | true      | false |
/// | `variadic_floats_in_gp`                 | false  | false** | true      | false |
/// | `large_struct_by_ref`                   | false  | true    | true      | false |
/// | `use_sysv_struct_classification`        | true   | false   | false     | false |
/// | `use_riscv_float_struct_classification`  | false  | false   | true      | false |
/// | `allow_struct_split_reg_stack`           | false  | false   | true      | false |
/// | `align_struct_pairs`                     | false  | false   | true      | false |
/// | `sret_uses_dedicated_reg`                | false  | true    | false     | false |
///
/// *i686 `max_int_regs` is 0 by default (cdecl); `-mregparm=N` raises it to 1–3.
///
/// **AArch64 variadic float promotion is handled by the caller setting
/// `variadic_floats_in_gp = true` specifically for variadic call sites, rather than
/// as a global ABI property. For non-variadic calls the flag is always false.
pub struct CallAbiConfig {
    /// Maximum number of general-purpose (integer/pointer) registers available for
    /// argument passing.
    ///
    /// - **x86-64 (SysV):** 6 — `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9`
    /// - **AArch64 (AAPCS64):** 8 — `x0`–`x7`
    /// - **RISC-V 64 (LP64D):** 8 — `a0`–`a7`
    /// - **i686 (cdecl):** 0 by default (all on stack); up to 3 (`eax`, `edx`, `ecx`)
    ///   when `-mregparm=N` is active
    pub max_int_regs: usize,
    /// Maximum number of floating-point registers available for argument passing.
    ///
    /// - **x86-64:** 8 — `xmm0`–`xmm7` (SSE registers)
    /// - **AArch64:** 8 — `v0`–`v7` / `d0`–`d7` (NEON/FP registers)
    /// - **RISC-V 64:** 8 — `fa0`–`fa7` (hardware FP registers)
    /// - **i686:** 0 — no FP argument registers; all floats passed on the x87 stack
    pub max_float_regs: usize,
    /// Whether `__int128` / `i128` register pairs must start at an even-numbered
    /// register index.
    ///
    /// - **AArch64:** true — AAPCS64 requires `__int128` in an even-aligned pair
    /// - **RISC-V 64:** true — LP64D requires 2×XLEN-aligned pairs at even registers
    /// - **x86-64:** false — SysV AMD64 has no pair alignment requirement
    /// - **i686:** false — i128 always goes on stack (no GP argument registers by default)
    pub align_i128_pairs: bool,
    /// Whether `long double` (`_Float128` / F128) values are passed in FP registers.
    ///
    /// - **AArch64:** true — F128 goes in Q-registers (`v0`–`v7`)
    /// - **x86-64:** false — x87 `long double` is 80-bit extended; always on x87 stack
    /// - **RISC-V 64:** false — F128 uses GP register pairs instead (see `f128_in_gp_pairs`)
    /// - **i686:** false — x87 `long double` is 80-bit extended, 12-byte padded, on stack
    pub f128_in_fp_regs: bool,
    /// Whether `long double` (F128) values are passed in GP register pairs (like `i128`).
    ///
    /// - **RISC-V 64:** true — `long double` is 128-bit IEEE 754 quad, passed in a GP pair
    ///   (`a0:a1`-style), subject to even-alignment via `align_i128_pairs`
    /// - **x86-64 / AArch64 / i686:** false
    pub f128_in_gp_pairs: bool,
    /// Whether floating-point arguments in variadic calls must be promoted to GP registers
    /// instead of being placed in FP registers.
    ///
    /// - **RISC-V 64:** true — variadic float args are passed in `a0`–`a7` GP registers
    /// - **AArch64:** conditionally true for variadic call sites (float args after the
    ///   last named parameter are promoted to GP registers per AAPCS64 §6.4)
    /// - **x86-64:** false — variadic floats go in XMM registers; the count is passed in `al`
    /// - **i686:** false — all arguments already on stack regardless
    pub variadic_floats_in_gp: bool,
    /// Whether large structs (>16 bytes) are passed by reference (pointer in a GP register)
    /// rather than being copied directly onto the stack.
    ///
    /// - **AArch64:** true — caller copies the struct to temporary storage and passes a
    ///   pointer in a GP register (or on the stack if GP registers are exhausted)
    /// - **RISC-V 64:** true — same by-reference semantics as AArch64
    /// - **x86-64:** false — large structs are classified as MEMORY and copied to the stack
    /// - **i686:** false — structs are pushed onto the stack by value
    pub large_struct_by_ref: bool,
    /// Whether to use the SysV AMD64 per-eightbyte struct classification.
    ///
    /// When true, each 8-byte chunk ("eightbyte") of a small struct (≤16 bytes) is
    /// independently classified as INTEGER or SSE based on its constituent fields.
    /// This allows structs like `struct { double x; int y; }` to be passed with the
    /// first half in an XMM register and the second half in a GP register.
    ///
    /// - **x86-64:** true — SysV AMD64 ABI §3.2.3 mandates per-eightbyte classification
    /// - **AArch64 / RISC-V 64 / i686:** false — these ABIs treat small structs as
    ///   opaque blobs in GP registers (or use the RISC-V float struct classification)
    pub use_sysv_struct_classification: bool,
    /// Whether to use RISC-V LP64D hardware floating-point struct classification.
    ///
    /// When true, small structs containing only float/double fields (up to 2 members)
    /// can be passed directly in FP registers (`fa0`–`fa7`). Mixed int+float structs
    /// with exactly one of each can split across a GP and an FP register.
    ///
    /// - **RISC-V 64:** true — RISC-V psABI §2.1 "Hardware Floating-Point Calling Convention"
    /// - **x86-64 / AArch64 / i686:** false
    pub use_riscv_float_struct_classification: bool,
    /// Whether a 2-register struct can be split across the last available GP register
    /// and the stack when only 1 GP register remains.
    ///
    /// - **RISC-V 64:** true — psABI §2.1: the first XLEN bytes go in the last GP register
    ///   and the remaining bytes are placed on the stack
    /// - **AArch64:** false — AAPCS64 does not split composites; if insufficient registers
    ///   remain, the entire struct goes on the stack
    /// - **x86-64 / i686:** false — structs are either fully in registers or fully on stack
    pub allow_struct_split_reg_stack: bool,
    /// Whether 2-register structs with alignment greater than XLEN must start at an
    /// even-numbered register index.
    ///
    /// - **RISC-V 64:** true — psABI requires 2×XLEN-aligned composites to use
    ///   even-aligned register pairs, mirroring the `__int128` alignment rule
    /// - **AArch64:** false — AAPCS64 does not require even-aligned pairs for composite
    ///   types; only fundamental types like `__int128` trigger even alignment (handled
    ///   separately by `align_i128_pairs`)
    /// - **x86-64 / i686:** false
    pub align_struct_pairs: bool,
    /// Whether struct return (sret) uses a dedicated register that does NOT consume
    /// a regular GP argument slot, freeing that slot for actual arguments.
    ///
    /// - **AArch64:** true — the sret pointer is passed in the dedicated `x8` register,
    ///   so all 8 GP argument registers (`x0`–`x7`) remain available for arguments.
    ///   When this is true, the classification must promote the first stack-overflow
    ///   GP argument to the freed register slot so that caller and callee agree.
    /// - **x86-64:** false — sret consumes `rdi` (the first GP argument register),
    ///   leaving 5 GP registers for remaining arguments
    /// - **RISC-V 64:** false — sret consumes `a0`, leaving 7 GP registers for arguments
    /// - **i686:** false — sret is pushed on the stack (callee pops)
    pub sret_uses_dedicated_reg: bool,
}

/// Result of SysV per-eightbyte struct classification.
/// Describes how a small struct (<=16 bytes) should be passed in registers.
#[derive(Debug, Clone, Copy)]
pub enum SysvStructRegClass {
    /// All eightbytes are INTEGER class -> GP registers only.
    AllInt,
    /// All eightbytes are SSE class -> XMM registers only.
    AllSse { fp_count: usize },
    /// First eightbyte INTEGER, second SSE (mixed).
    IntSse,
    /// First eightbyte SSE, second INTEGER (mixed).
    SseInt,
    /// Not enough registers available -> spill to stack.
    Stack,
}

/// Classify a small struct (<=16 bytes) using SysV AMD64 per-eightbyte rules.
///
/// Given the eightbyte classes and current register allocation state, determines
/// whether the struct fits in registers and which class combination to use.
/// Returns the classification and the number of GP/FP registers consumed.
pub fn classify_sysv_struct(
    eb_classes: &[crate::common::types::EightbyteClass],
    int_idx: usize,
    float_idx: usize,
    config: &CallAbiConfig,
) -> (SysvStructRegClass, usize, usize) {
    use crate::common::types::EightbyteClass;
    let n_eightbytes = eb_classes.len();
    let eb0_is_sse = eb_classes.first() == Some(&EightbyteClass::Sse);
    let eb1_is_sse = if n_eightbytes > 1 { eb_classes.get(1) == Some(&EightbyteClass::Sse) } else { false };

    let gp_needed = (if !eb0_is_sse { 1 } else { 0 })
        + (if n_eightbytes > 1 && !eb1_is_sse { 1 } else { 0 });
    let fp_needed = (if eb0_is_sse { 1 } else { 0 })
        + (if n_eightbytes > 1 && eb1_is_sse { 1 } else { 0 });

    if int_idx + gp_needed > config.max_int_regs || float_idx + fp_needed > config.max_float_regs {
        return (SysvStructRegClass::Stack, 0, 0);
    }

    if n_eightbytes == 1 {
        if eb0_is_sse {
            (SysvStructRegClass::AllSse { fp_count: 1 }, 0, 1)
        } else {
            (SysvStructRegClass::AllInt, 1, 0)
        }
    } else if eb0_is_sse && eb1_is_sse {
        (SysvStructRegClass::AllSse { fp_count: 2 }, 0, 2)
    } else if !eb0_is_sse && eb1_is_sse {
        (SysvStructRegClass::IntSse, 1, 1)
    } else if eb0_is_sse && !eb1_is_sse {
        (SysvStructRegClass::SseInt, 1, 1)
    } else {
        (SysvStructRegClass::AllInt, 2, 0)
    }
}

// ---------------------------------------------------------------------------
// ArgInfo — abstracts per-argument metadata for the unified classification core
// ---------------------------------------------------------------------------

/// Per-argument metadata needed by the classification algorithm.
///
/// Both the caller (`classify_call_args`) and callee (`classify_params_full`) paths
/// construct one `ArgInfo` per argument/parameter, then pass the slice to the shared
/// `classify_args_core` function.
pub(crate) struct ArgInfo<'a> {
    pub(crate) is_float: bool,
    pub(crate) is_i128: bool,
    pub(crate) is_long_double: bool,
    /// If this is a struct/union by value: Some(byte_size). None otherwise.
    pub(crate) struct_size: Option<usize>,
    /// Struct alignment in bytes (for RISC-V even-register alignment). None for non-struct.
    pub(crate) struct_align: Option<usize>,
    /// SysV per-eightbyte classification (x86-64 only). Empty if not applicable.
    pub(crate) eightbyte_classes: &'a [crate::common::types::EightbyteClass],
    /// RISC-V LP64D float struct classification. None if not applicable.
    pub(crate) riscv_float_class: Option<crate::common::types::RiscvFloatClass>,
}

// ---------------------------------------------------------------------------
// Core classification algorithm — single implementation used by both sides
// ---------------------------------------------------------------------------

/// Internal classification result for one argument, used by the core algorithm.
/// Represents the ABI decision *without* stack offsets — offsets are layered on
/// by the callee-side wrapper (`classify_params_full`).
#[derive(Debug, Clone, Copy)]
enum CoreArgClass {
    IntReg { reg_idx: usize },
    FloatReg { reg_idx: usize },
    I128RegPair { base_reg_idx: usize },
    F128FpReg { reg_idx: usize },
    F128GpPair { base_reg_idx: usize },
    F128Stack,
    StructByValReg { base_reg_idx: usize, size: usize },
    StructSseReg { lo_fp_idx: usize, hi_fp_idx: Option<usize>, size: usize },
    StructMixedIntSseReg { int_reg_idx: usize, fp_reg_idx: usize, size: usize },
    StructMixedSseIntReg { fp_reg_idx: usize, int_reg_idx: usize, size: usize },
    StructByValStack { size: usize },
    StructSplitRegStack { reg_idx: usize, size: usize },
    LargeStructStack { size: usize },
    LargeStructByRefReg { reg_idx: usize, size: usize },
    LargeStructByRefStack { size: usize },
    Stack,
    I128Stack,
    ZeroSizeSkip,
}

/// Result of the core classification algorithm.
struct CoreClassification {
    classes: Vec<CoreArgClass>,
    /// Final GP register index after classifying all arguments (capped at max_int_regs).
    int_reg_idx: usize,
}

/// Core ABI classification algorithm shared by both caller and callee paths.
///
/// Walks the argument list and assigns each to a register or stack class based on
/// the ABI configuration. The `is_variadic` flag gates the `force_gp` behavior for
/// float args in variadic functions (needed on the caller side; the callee side
/// encodes this in `config.variadic_floats_in_gp` directly).
fn classify_args_core(
    args: &[ArgInfo<'_>],
    is_variadic: bool,
    config: &CallAbiConfig,
) -> CoreClassification {
    let mut result = Vec::with_capacity(args.len());
    let mut int_idx = 0usize;
    let mut float_idx = 0usize;
    let slot_size = crate::common::types::target_ptr_size();

    for info in args {
        let force_gp = is_variadic && config.variadic_floats_in_gp && info.is_float && !info.is_long_double;

        if let Some(size) = info.struct_size {
            // Zero-size structs consume no register or stack space per GCC behavior.
            if size == 0 {
                result.push(CoreArgClass::ZeroSizeSkip);
                continue;
            }

            let eb_classes = info.eightbyte_classes;

            if size <= 16 && config.use_sysv_struct_classification && !eb_classes.is_empty() {
                // SysV AMD64 ABI: classify per-eightbyte and assign to GP or SSE registers
                let (cls, gp_used, fp_used) = classify_sysv_struct(eb_classes, int_idx, float_idx, config);
                match cls {
                    SysvStructRegClass::AllSse { fp_count } => {
                        let hi = if fp_count > 1 { Some(float_idx + 1) } else { None };
                        result.push(CoreArgClass::StructSseReg { lo_fp_idx: float_idx, hi_fp_idx: hi, size });
                    }
                    SysvStructRegClass::AllInt => {
                        result.push(CoreArgClass::StructByValReg { base_reg_idx: int_idx, size });
                    }
                    SysvStructRegClass::IntSse => {
                        result.push(CoreArgClass::StructMixedIntSseReg { int_reg_idx: int_idx, fp_reg_idx: float_idx, size });
                    }
                    SysvStructRegClass::SseInt => {
                        result.push(CoreArgClass::StructMixedSseIntReg { fp_reg_idx: float_idx, int_reg_idx: int_idx, size });
                    }
                    SysvStructRegClass::Stack => {
                        result.push(CoreArgClass::StructByValStack { size });
                        int_idx = config.max_int_regs;
                    }
                }
                int_idx += gp_used;
                float_idx += fp_used;
            } else if size <= 16 {
                // Non-SysV path (ARM, RISC-V)
                let rv_class = if config.use_riscv_float_struct_classification {
                    info.riscv_float_class
                } else {
                    None
                };
                let mut classified = false;
                if let Some(rv_fc) = rv_class {
                    use crate::common::types::RiscvFloatClass;
                    match rv_fc {
                        RiscvFloatClass::OneFloat { .. } => {
                            if float_idx < config.max_float_regs {
                                result.push(CoreArgClass::StructSseReg { lo_fp_idx: float_idx, hi_fp_idx: None, size });
                                float_idx += 1;
                                classified = true;
                            }
                        }
                        RiscvFloatClass::TwoFloats { .. } => {
                            if float_idx + 1 < config.max_float_regs {
                                result.push(CoreArgClass::StructSseReg { lo_fp_idx: float_idx, hi_fp_idx: Some(float_idx + 1), size });
                                float_idx += 2;
                                classified = true;
                            }
                        }
                        RiscvFloatClass::FloatAndInt { .. } => {
                            if float_idx < config.max_float_regs && int_idx < config.max_int_regs {
                                result.push(CoreArgClass::StructMixedSseIntReg { fp_reg_idx: float_idx, int_reg_idx: int_idx, size });
                                float_idx += 1;
                                int_idx += 1;
                                classified = true;
                            }
                        }
                        RiscvFloatClass::IntAndFloat { .. } => {
                            if int_idx < config.max_int_regs && float_idx < config.max_float_regs {
                                result.push(CoreArgClass::StructMixedIntSseReg { int_reg_idx: int_idx, fp_reg_idx: float_idx, size });
                                int_idx += 1;
                                float_idx += 1;
                                classified = true;
                            }
                        }
                    }
                }
                if !classified {
                    let regs_needed = if size <= slot_size { 1 } else { size.div_ceil(slot_size) };
                    // RISC-V psABI: 2×XLEN-aligned structs must start at even register.
                    // Note: ARM AAPCS64 does NOT require even-aligned pairs for composites.
                    if regs_needed == 2 && config.align_struct_pairs {
                        let struct_align = info.struct_align.unwrap_or(slot_size);
                        if struct_align > slot_size && !int_idx.is_multiple_of(2) {
                            int_idx += 1; // skip to even register
                        }
                    }
                    if int_idx + regs_needed <= config.max_int_regs {
                        result.push(CoreArgClass::StructByValReg { base_reg_idx: int_idx, size });
                        int_idx += regs_needed;
                    } else if regs_needed == 2 && int_idx < config.max_int_regs && config.allow_struct_split_reg_stack {
                        result.push(CoreArgClass::StructSplitRegStack { reg_idx: int_idx, size });
                        int_idx = config.max_int_regs;
                    } else {
                        result.push(CoreArgClass::StructByValStack { size });
                        int_idx = config.max_int_regs;
                    }
                }
            } else if config.large_struct_by_ref {
                // AAPCS64 / RISC-V: large composites passed by reference.
                if int_idx < config.max_int_regs {
                    result.push(CoreArgClass::LargeStructByRefReg { reg_idx: int_idx, size });
                    int_idx += 1;
                } else {
                    result.push(CoreArgClass::LargeStructByRefStack { size });
                }
            } else {
                result.push(CoreArgClass::LargeStructStack { size });
            }
        } else if info.is_i128 {
            if config.align_i128_pairs && !int_idx.is_multiple_of(2) {
                int_idx += 1;
            }
            if int_idx + 1 < config.max_int_regs {
                result.push(CoreArgClass::I128RegPair { base_reg_idx: int_idx });
                int_idx += 2;
            } else {
                result.push(CoreArgClass::I128Stack);
                int_idx = config.max_int_regs;
            }
        } else if info.is_long_double {
            if config.f128_in_fp_regs {
                if float_idx < config.max_float_regs {
                    result.push(CoreArgClass::F128FpReg { reg_idx: float_idx });
                    float_idx += 1;
                } else {
                    result.push(CoreArgClass::F128Stack);
                }
            } else if config.f128_in_gp_pairs {
                if config.align_i128_pairs && !int_idx.is_multiple_of(2) {
                    int_idx += 1;
                }
                if int_idx + 1 < config.max_int_regs {
                    result.push(CoreArgClass::F128GpPair { base_reg_idx: int_idx });
                    int_idx += 2;
                } else {
                    result.push(CoreArgClass::F128Stack);
                    int_idx = config.max_int_regs;
                }
            } else {
                result.push(CoreArgClass::F128Stack);
            }
        } else if info.is_float && !force_gp && float_idx < config.max_float_regs {
            result.push(CoreArgClass::FloatReg { reg_idx: float_idx });
            float_idx += 1;
        } else if info.is_float && !force_gp {
            result.push(CoreArgClass::Stack);
        } else if int_idx < config.max_int_regs {
            result.push(CoreArgClass::IntReg { reg_idx: int_idx });
            int_idx += 1;
        } else {
            result.push(CoreArgClass::Stack);
        }
    }

    CoreClassification { classes: result, int_reg_idx: int_idx }
}

// ---------------------------------------------------------------------------
// Caller-side wrapper: classify_call_args
// ---------------------------------------------------------------------------

/// Classify all arguments for a function call, returning a `CallArgClass` per argument.
///
/// This is the caller-side entry point. It extracts `ArgInfo` from the call-site
/// arrays and delegates to the shared classification core.
///
/// `struct_arg_sizes`: Some(size) for struct/union by-value args, None otherwise.
/// `struct_arg_aligns`: struct alignment (for RISC-V even-register alignment).
/// `struct_arg_classes`: per-eightbyte SysV ABI classification (x86-64 only).
/// `struct_arg_riscv_float_classes`: RISC-V LP64D float field classification.
pub fn classify_call_args(
    args: &[Operand],
    arg_types: &[IrType],
    struct_arg_sizes: &[Option<usize>],
    struct_arg_aligns: &[Option<usize>],
    struct_arg_classes: &[Vec<crate::common::types::EightbyteClass>],
    struct_arg_riscv_float_classes: &[Option<crate::common::types::RiscvFloatClass>],
    is_variadic: bool,
    config: &CallAbiConfig,
) -> Vec<CallArgClass> {
    // Build ArgInfo slice from call-site arrays.
    let arg_infos: Vec<ArgInfo<'_>> = args.iter().enumerate().map(|(i, arg)| {
        let arg_ty = if i < arg_types.len() { Some(arg_types[i]) } else { None };
        ArgInfo {
            is_float: if let Some(ty) = arg_ty {
                ty.is_float()
            } else {
                matches!(arg, Operand::Const(IrConst::F32(_) | IrConst::F64(_)))
            },
            is_i128: arg_ty.map(is_i128_type).unwrap_or(false),
            is_long_double: arg_ty.map(|t| t.is_long_double()).unwrap_or(false),
            struct_size: struct_arg_sizes.get(i).copied().flatten(),
            struct_align: struct_arg_aligns.get(i).copied().flatten(),
            eightbyte_classes: struct_arg_classes.get(i).map(|v| v.as_slice()).unwrap_or(&[]),
            riscv_float_class: struct_arg_riscv_float_classes.get(i).copied().flatten(),
        }
    }).collect();

    let core = classify_args_core(&arg_infos, is_variadic, config);

    // Convert CoreArgClass -> CallArgClass (drop by-ref variants that only appear
    // when large_struct_by_ref is true, which maps to IntReg/Stack on the caller side).
    core.classes.into_iter().map(|c| match c {
        CoreArgClass::IntReg { reg_idx } => CallArgClass::IntReg { reg_idx },
        CoreArgClass::FloatReg { reg_idx } => CallArgClass::FloatReg { reg_idx },
        CoreArgClass::I128RegPair { base_reg_idx } => CallArgClass::I128RegPair { base_reg_idx },
        CoreArgClass::F128FpReg { reg_idx } | CoreArgClass::F128GpPair { base_reg_idx: reg_idx } => CallArgClass::F128Reg { reg_idx },
        CoreArgClass::F128Stack => CallArgClass::F128Stack,
        CoreArgClass::StructByValReg { base_reg_idx, size } => CallArgClass::StructByValReg { base_reg_idx, size },
        CoreArgClass::StructSseReg { lo_fp_idx, hi_fp_idx, size } => CallArgClass::StructSseReg { lo_fp_idx, hi_fp_idx, size },
        CoreArgClass::StructMixedIntSseReg { int_reg_idx, fp_reg_idx, size } => CallArgClass::StructMixedIntSseReg { int_reg_idx, fp_reg_idx, size },
        CoreArgClass::StructMixedSseIntReg { fp_reg_idx, int_reg_idx, size } => CallArgClass::StructMixedSseIntReg { fp_reg_idx, int_reg_idx, size },
        CoreArgClass::StructByValStack { size } => CallArgClass::StructByValStack { size },
        CoreArgClass::StructSplitRegStack { reg_idx, size } => CallArgClass::StructSplitRegStack { reg_idx, size },
        CoreArgClass::LargeStructStack { size } => CallArgClass::LargeStructStack { size },
        // On caller side, large-struct-by-ref uses IntReg (pointer) or Stack (pointer overflow).
        CoreArgClass::LargeStructByRefReg { reg_idx, .. } => CallArgClass::IntReg { reg_idx },
        CoreArgClass::LargeStructByRefStack { .. } => CallArgClass::Stack,
        CoreArgClass::Stack => CallArgClass::Stack,
        CoreArgClass::I128Stack => CallArgClass::I128Stack,
        CoreArgClass::ZeroSizeSkip => CallArgClass::ZeroSizeSkip,
    }).collect()
}

// ---------------------------------------------------------------------------
// Callee-side wrapper: classify_params_full / classify_params
// ---------------------------------------------------------------------------

/// Result of parameter classification, including the final register allocation state.
/// The `int_reg_idx` field captures the effective GP register index after all named
/// params are classified, which is needed by RISC-V va_start to correctly skip
/// alignment padding gaps (e.g., when an F128 pair couldn't fit and bumped the index).
pub struct ParamClassification {
    pub classes: Vec<ParamClass>,
    /// Final GP register index after classifying all named params.
    /// Includes alignment bumps for I128/F128 pairs. Capped at max_int_regs.
    pub int_reg_idx: usize,
    /// Total stack bytes consumed by all named parameters.
    /// This is the final stack_offset after classification, accounting for
    /// type-specific sizes (e.g., F64/I64 take 8 bytes on ILP32).
    pub total_stack_bytes: usize,
}

/// Classify all parameters of a function for callee-side store emission.
///
/// Uses the same `CallAbiConfig` as `classify_call_args` to ensure caller and callee
/// agree on parameter locations. Returns one `ParamClass` per parameter plus the final
/// register allocation state.
pub fn classify_params_full(func: &IrFunction, config: &CallAbiConfig) -> ParamClassification {
    // Build ArgInfo slice from function parameters.
    let arg_infos: Vec<ArgInfo<'_>> = func.params.iter().map(|param| {
        ArgInfo {
            is_float: param.ty.is_float(),
            is_i128: is_i128_type(param.ty),
            is_long_double: param.ty.is_long_double(),
            struct_size: param.struct_size,
            struct_align: param.struct_align,
            eightbyte_classes: &param.struct_eightbyte_classes,
            riscv_float_class: param.riscv_float_class,
        }
    }).collect();

    // Pass is_variadic=true here because the callee side encodes variadic behavior
    // directly in config.variadic_floats_in_gp (set by the caller based on func.is_variadic).
    // The force_gp check is: is_variadic && config.variadic_floats_in_gp && is_float.
    // For non-variadic functions, config.variadic_floats_in_gp is false, so force_gp
    // is false regardless of the is_variadic flag — making this safe.
    let mut core = classify_args_core(&arg_infos, true, config);

    // AArch64 ABI: when sret uses a dedicated register (x8), the classification initially
    // assigns the sret pointer to IntReg(0), consuming one GP register slot. On the caller
    // side (emit_call in traits.rs), the sret arg is reclassified to ZeroSizeSkip, all
    // other GP reg indices are shifted down by 1, and the first stack-overflow GP arg is
    // promoted to the freed register slot (max_int_regs-1, i.e. x7).
    //
    // We must apply the same promotion on the callee side so that caller and callee agree
    // on where each argument lives. The emit_store_gp_params method already handles
    // the register index shift (sret_shift=1), but it does NOT promote stack args.
    // Apply the promotion here to match the caller-side logic.
    if func.uses_sret && config.sret_uses_dedicated_reg && core.classes.len() > 1 {
        // Use max_int_regs (not max_int_regs-1) because emit_store_gp_params applies
        // sret_shift=1, computing actual_idx = reg_idx - 1. So reg_idx=8 maps to x7.
        let freed_reg = config.max_int_regs;
        // Promote the first GP stack-overflow arg to the freed register slot.
        for i in 1..core.classes.len() {
            match core.classes[i] {
                CoreArgClass::Stack => {
                    let is_float = func.params.get(i).map(|p| p.ty.is_float()).unwrap_or(false);
                    if !is_float {
                        core.classes[i] = CoreArgClass::IntReg { reg_idx: freed_reg };
                        break;
                    }
                }
                CoreArgClass::StructByValStack { size } if size <= 8 => {
                    core.classes[i] = CoreArgClass::StructByValReg { base_reg_idx: freed_reg, size };
                    break;
                }
                _ => {}
            }
        }
    }

    // Convert CoreArgClass -> ParamClass, assigning stack offsets.
    let slot_size = crate::common::types::target_ptr_size();
    let slot_align_mask = (slot_size - 1) as i64;
    let mut stack_offset: i64 = 0;
    let mut classes = Vec::with_capacity(core.classes.len());

    for (i, c) in core.classes.iter().enumerate() {
        let param_ty = func.params.get(i).map(|p| p.ty);
        let pc = match *c {
            CoreArgClass::IntReg { reg_idx } => ParamClass::IntReg { reg_idx },
            CoreArgClass::FloatReg { reg_idx } => ParamClass::FloatReg { reg_idx },
            CoreArgClass::I128RegPair { base_reg_idx } => ParamClass::I128RegPair { base_reg_idx },
            CoreArgClass::StructByValReg { base_reg_idx, size } => ParamClass::StructByValReg { base_reg_idx, size },
            CoreArgClass::StructSseReg { lo_fp_idx, hi_fp_idx, size } => ParamClass::StructSseReg { lo_fp_idx, hi_fp_idx, size },
            CoreArgClass::StructMixedIntSseReg { int_reg_idx, fp_reg_idx, size } => ParamClass::StructMixedIntSseReg { int_reg_idx, fp_reg_idx, size },
            CoreArgClass::StructMixedSseIntReg { fp_reg_idx, int_reg_idx, size } => ParamClass::StructMixedSseIntReg { fp_reg_idx, int_reg_idx, size },
            CoreArgClass::F128FpReg { reg_idx } => ParamClass::F128FpReg { reg_idx },
            CoreArgClass::F128GpPair { base_reg_idx } => ParamClass::F128GpPair { lo_reg_idx: base_reg_idx, hi_reg_idx: base_reg_idx + 1 },
            CoreArgClass::LargeStructByRefReg { reg_idx, size } => ParamClass::LargeStructByRefReg { reg_idx, size },
            CoreArgClass::ZeroSizeSkip => ParamClass::ZeroSizeSkip,

            // Stack-overflow cases: assign offsets
            CoreArgClass::F128Stack => {
                if config.f128_in_fp_regs || config.f128_in_gp_pairs {
                    // ARM/RISC-V: F128 overflowed from registers
                    stack_offset = (stack_offset + 15) & !15;
                    let off = stack_offset;
                    stack_offset += 16;
                    ParamClass::F128Stack { offset: off }
                } else if slot_size == 4 {
                    // i686: long double is 12 bytes, 4-byte aligned
                    let off = stack_offset;
                    stack_offset += 12;
                    ParamClass::F128AlwaysStack { offset: off }
                } else {
                    // x86-64: 16 bytes, 16-byte aligned
                    stack_offset = (stack_offset + 15) & !15;
                    let off = stack_offset;
                    stack_offset += 16;
                    ParamClass::F128AlwaysStack { offset: off }
                }
            }
            CoreArgClass::I128Stack => {
                stack_offset = (stack_offset + 15) & !15;
                let off = stack_offset;
                stack_offset += 16;
                ParamClass::I128Stack { offset: off }
            }
            CoreArgClass::StructByValStack { size } | CoreArgClass::LargeStructStack { size } => {
                let off = stack_offset;
                stack_offset += (size as i64 + slot_align_mask) & !slot_align_mask;
                if matches!(*c, CoreArgClass::LargeStructStack { .. }) {
                    ParamClass::LargeStructStack { offset: off, size }
                } else {
                    ParamClass::StructStack { offset: off, size }
                }
            }
            CoreArgClass::StructSplitRegStack { reg_idx, size } => {
                let off = stack_offset;
                let stack_part = size - slot_size;
                stack_offset += (stack_part as i64 + slot_align_mask) & !slot_align_mask;
                ParamClass::StructSplitRegStack { reg_idx, stack_offset: off, size }
            }
            CoreArgClass::LargeStructByRefStack { size } => {
                let off = stack_offset;
                stack_offset += slot_size as i64; // pointer on stack
                ParamClass::LargeStructByRefStack { offset: off, size }
            }
            CoreArgClass::Stack => {
                let off = stack_offset;
                let is_float = param_ty.map(|t| t.is_float()).unwrap_or(false);
                if is_float {
                    // Float that overflowed FP registers
                    if slot_size == 4 {
                        let float_stack_size = if param_ty == Some(IrType::F64) { 8 } else { 4 };
                        stack_offset += float_stack_size;
                    } else {
                        stack_offset += 8;
                    }
                } else {
                    // GP register overflow
                    let param_size = param_ty.map(|t| t.size() as i64).unwrap_or(slot_size as i64);
                    stack_offset += (param_size + slot_align_mask) & !slot_align_mask;
                }
                ParamClass::StackScalar { offset: off }
            }
        };
        classes.push(pc);
    }

    ParamClassification {
        classes,
        int_reg_idx: core.int_reg_idx,
        total_stack_bytes: stack_offset as usize,
    }
}

/// Classify all parameters of a function for callee-side store emission.
///
/// Uses the same `CallAbiConfig` as `classify_call_args` to ensure caller and callee
/// agree on parameter locations. Returns one `ParamClass` per parameter.
pub fn classify_params(func: &IrFunction, config: &CallAbiConfig) -> Vec<ParamClass> {
    classify_params_full(func, config).classes
}

/// Compute the total stack space (in bytes) consumed by named parameters that are
/// passed on the stack. This is needed for variadic functions: va_start must set its
/// stack pointer past all named stack-passed args to point at the first variadic arg.
///
/// This correctly accounts for alignment padding (e.g., 16-byte alignment for F128/I128).
pub fn named_params_stack_bytes(param_classes: &[ParamClass]) -> usize {
    let mut total: usize = 0;
    for class in param_classes {
        // Align for 16-byte types before adding their size
        if matches!(class, ParamClass::F128Stack { .. } | ParamClass::I128Stack { .. } | ParamClass::F128AlwaysStack { .. }) {
            total = (total + 15) & !15;
        }
        total += class.stack_bytes();
    }
    total
}

// ---------------------------------------------------------------------------
// Call-site stack space helpers (used by emit_call)
// ---------------------------------------------------------------------------

/// Compute the total stack space needed for stack-overflow arguments.
/// Returns the total bytes needed, 16-byte aligned.
/// Use this for ARM and RISC-V which pre-allocate stack space with a single SP adjustment.
pub fn compute_stack_arg_space(arg_classes: &[CallArgClass]) -> usize {
    let mut total: usize = 0;
    for cls in arg_classes {
        if !cls.is_stack() { continue; }
        if matches!(cls, CallArgClass::F128Stack | CallArgClass::I128Stack) {
            total = (total + 15) & !15;
        }
        total += cls.stack_bytes();
    }
    (total + 15) & !15
}

/// Compute per-stack-arg alignment padding needed in the forward layout.
/// Returns a Vec with one entry per `arg_classes` element. Non-stack args get 0.
/// F128Stack and I128Stack args get padding to align to 16 bytes in the overflow area.
pub fn compute_stack_arg_padding(arg_classes: &[CallArgClass]) -> Vec<usize> {
    let mut padding = vec![0usize; arg_classes.len()];
    let mut offset: usize = 0;
    for (i, cls) in arg_classes.iter().enumerate() {
        if !cls.is_stack() { continue; }
        if matches!(cls, CallArgClass::F128Stack | CallArgClass::I128Stack) {
            let align_pad = (16 - (offset % 16)) % 16;
            padding[i] = align_pad;
            offset += align_pad;
        }
        offset += cls.stack_bytes();
    }
    padding
}

/// Compute the raw bytes that will be pushed onto the stack for stack arguments.
/// Unlike `compute_stack_arg_space`, this does NOT apply final 16-byte alignment,
/// because x86 uses individual `pushq` instructions and handles alignment separately.
/// This includes alignment padding for F128/I128 args.
pub fn compute_stack_push_bytes(arg_classes: &[CallArgClass]) -> usize {
    let padding = compute_stack_arg_padding(arg_classes);
    let mut total: usize = 0;
    for (i, cls) in arg_classes.iter().enumerate() {
        if !cls.is_stack() { continue; }
        total += padding[i] + cls.stack_bytes();
    }
    total
}
