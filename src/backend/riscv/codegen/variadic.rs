//! RiscvCodegen: va_arg, va_start, va_copy operations.
//!
//! ## RISC-V LP64D Variadic Argument Passing
//!
//! On RISC-V LP64D, `va_list` is a simple `void*` pointing to the next
//! argument in the stack overflow area. The register save area holds a0–a7
//! (64 bytes) above the frame pointer; stack overflow arguments follow.
//!
//! ### Alignment Contract
//!
//! Types requiring >8-byte alignment (I128, F128, and structs containing
//! them) must be 16-byte aligned on the variadic argument stack. This is
//! a caller/callee contract:
//!
//! - **Caller** (`calls.rs`): Must place ≥16-byte-aligned types at
//!   16-byte-aligned stack offsets, inserting padding as needed.
//! - **Callee** (`emit_va_arg_impl` here): Aligns the va_list pointer
//!   for direct F128 and I128 reads.
//!
//! ### Struct va_arg Path
//!
//! Struct variadic arguments do NOT flow through `emit_va_arg_impl` as
//! whole structs. Instead, the IR lowering in `lower_va_arg_struct`
//! (expr_access.rs) decomposes struct reads into individual 8-byte
//! `VaArg { result_ty: IrType::I64 }` slot reads. Struct alignment is
//! handled at the IR level (before the slot reads) and at the caller
//! level (proper stack padding). Each slot read enters the standard
//! 8-byte path in `emit_va_arg_impl`.

use crate::ir::reexports::Value;
use crate::common::types::IrType;
use super::emit::{RiscvCodegen, callee_saved_name};

impl RiscvCodegen {
    pub(super) fn emit_va_arg_impl(&mut self, dest: &Value, va_list_ptr: &Value, result_ty: IrType) {
        // RISC-V LP64D: va_list is just a void* (pointer to the next arg on stack).
        if self.state.is_alloca(va_list_ptr.0) {
            if let Some(slot) = self.state.get_slot(va_list_ptr.0) {
                self.emit_addi_s0("t1", slot.0);
            }
        } else if let Some(&reg) = self.reg_assignments.get(&va_list_ptr.0) {
            let reg_name = callee_saved_name(reg);
            self.state.emit_fmt(format_args!("    mv t1, {}", reg_name));
        } else if let Some(slot) = self.state.get_slot(va_list_ptr.0) {
            self.emit_load_from_s0("t1", slot.0, "ld");
        }
        // Load the current va_list pointer value (points to next arg)
        self.state.emit("    ld t2, 0(t1)");

        // I128/U128: 16 bytes, 16-byte aligned on RISC-V LP64D.
        // Like F128, __int128 needs 16-byte alignment when read from the
        // variadic argument stack. Unlike F128 there is no float conversion;
        // we simply load both 64-bit halves and store via store_t0_t1_to.
        if matches!(result_ty, IrType::I128 | IrType::U128) {
            self.state.emit("    addi t2, t2, 15");
            self.state.emit("    andi t2, t2, -16");
            self.state.emit("    ld t0, 0(t2)");      // low 64 bits
            self.state.emit("    ld t3, 8(t2)");      // high 64 bits (temp in t3)
            self.state.emit("    addi t2, t2, 16");
            self.state.emit("    sd t2, 0(t1)");      // advance va_list
            self.state.emit("    mv t1, t3");          // move high half to t1
            self.store_t0_t1_to(dest);
            return;
        }

        if result_ty.is_long_double() {
            // F128 (long double): 16 bytes, 16-byte aligned.
            self.state.emit("    addi t2, t2, 15");
            self.state.emit("    andi t2, t2, -16");
            self.state.emit("    ld a0, 0(t2)");
            self.state.emit("    ld a1, 8(t2)");
            self.state.emit("    addi t2, t2, 16");
            self.state.emit("    sd t2, 0(t1)");
            self.state.emit("    call __trunctfdf2");
            self.state.emit("    fmv.x.d t0, fa0");
        } else {
            // Standard 8-byte arg
            self.state.emit("    ld t0, 0(t2)");
            self.state.emit("    addi t2, t2, 8");
            self.state.emit("    sd t2, 0(t1)");
        }
        self.store_t0_to(dest);
    }

    pub(super) fn emit_va_start_impl(&mut self, va_list_ptr: &Value) {
        if self.state.is_alloca(va_list_ptr.0) {
            if let Some(slot) = self.state.get_slot(va_list_ptr.0) {
                self.emit_addi_s0("t0", slot.0);
            }
        } else if let Some(&reg) = self.reg_assignments.get(&va_list_ptr.0) {
            let reg_name = callee_saved_name(reg);
            self.state.emit_fmt(format_args!("    mv t0, {}", reg_name));
        } else if let Some(slot) = self.state.get_slot(va_list_ptr.0) {
            self.emit_load_from_s0("t0", slot.0, "ld");
        }

        let vararg_offset = if self.va_named_gp_count >= 8 {
            64 + self.va_named_stack_bytes as i64
        } else {
            (self.va_named_gp_count as i64) * 8
        };
        self.emit_addi_s0("t1", vararg_offset);
        self.state.emit("    sd t1, 0(t0)");
    }

    pub(super) fn emit_va_copy_impl(&mut self, dest_ptr: &Value, src_ptr: &Value) {
        if self.state.is_alloca(src_ptr.0) {
            if let Some(src_slot) = self.state.get_slot(src_ptr.0) {
                self.emit_addi_s0("t1", src_slot.0);
            }
        } else if let Some(&reg) = self.reg_assignments.get(&src_ptr.0) {
            let reg_name = callee_saved_name(reg);
            self.state.emit_fmt(format_args!("    mv t1, {}", reg_name));
        } else if let Some(src_slot) = self.state.get_slot(src_ptr.0) {
            self.emit_load_from_s0("t1", src_slot.0, "ld");
        }
        self.state.emit("    ld t2, 0(t1)");
        if self.state.is_alloca(dest_ptr.0) {
            if let Some(dest_slot) = self.state.get_slot(dest_ptr.0) {
                self.emit_addi_s0("t0", dest_slot.0);
            }
        } else if let Some(&reg) = self.reg_assignments.get(&dest_ptr.0) {
            let reg_name = callee_saved_name(reg);
            self.state.emit_fmt(format_args!("    mv t0, {}", reg_name));
        } else if let Some(dest_slot) = self.state.get_slot(dest_ptr.0) {
            self.emit_load_from_s0("t0", dest_slot.0, "ld");
        }
        self.state.emit("    sd t2, 0(t0)");
    }
}
