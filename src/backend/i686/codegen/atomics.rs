//! I686Codegen: atomic operations (RMW, cmpxchg, load, store, fence).
//!
//! Implements C11 _Atomic operations for i686 using x86 LOCK-prefixed instructions:
//! - LOCK XCHG for exchange (implicit LOCK prefix)
//! - LOCK XADD for fetch_add/fetch_sub
//! - LOCK CMPXCHG for compare_exchange (always strong on x86)
//! - LOCK CMPXCHG8B for 64-bit atomics (via wide helpers in emit.rs)
//! - CAS retry loops for min/max/and/or/xor/nand operations
//! - MFENCE for sequential consistency fences
//!
//! x86 memory model: loads are acquire, stores are release, LOCK instructions
//! are full barriers. AtomicOrdering::Consume is treated as Acquire per C11
//! recommendation (§7.17.3p3), which on x86 is a hardware no-op.

use crate::ir::reexports::{AtomicOrdering, AtomicRmwOp, Operand, Value};
use crate::common::types::IrType;
use crate::emit;
use super::emit::I686Codegen;

impl I686Codegen {
    pub(super) fn emit_atomic_rmw_impl(&mut self, dest: &Value, op: AtomicRmwOp, ptr: &Operand, val: &Operand,
                       ty: IrType, _ordering: AtomicOrdering) {
        if self.is_atomic_wide(ty) {
            self.emit_atomic_rmw_wide(dest, op, ptr, val);
            return;
        }

        self.operand_to_eax(val);
        self.state.emit("    movl %eax, %edx");
        self.operand_to_eax(ptr);
        self.state.emit("    movl %eax, %ecx");

        match op {
            AtomicRmwOp::Xchg => {
                let suffix = self.type_suffix(ty);
                let reg = self.eax_for_type(ty);
                self.state.emit("    movl %edx, %eax");
                emit!(self.state, "    xchg{} {}, (%ecx)", suffix, reg);
            }
            AtomicRmwOp::TestAndSet => {
                self.state.emit("    movb $1, %al");
                self.state.emit("    xchgb %al, (%ecx)");
                // Zero-extend %al to %eax: xchgb only sets the low byte,
                // leaving upper bytes with garbage from prior register usage.
                self.state.emit("    movzbl %al, %eax");
            }
            AtomicRmwOp::Add => {
                let suffix = self.type_suffix(ty);
                let reg = match ty {
                    IrType::I8 | IrType::U8 => "%dl",
                    IrType::I16 | IrType::U16 => "%dx",
                    _ => "%edx",
                };
                emit!(self.state, "    lock xadd{} {}, (%ecx)", suffix, reg);
                self.state.emit("    movl %edx, %eax");
            }
            AtomicRmwOp::Sub => {
                // fetch_sub via lock xadd with negated value: more efficient than CAS loop.
                // lock xadd atomically does: old = *ptr; *ptr = *ptr + reg; reg = old
                // By negating the operand first, we get: *ptr = *ptr + (-val) = *ptr - val
                let suffix = self.type_suffix(ty);
                let reg = match ty {
                    IrType::I8 | IrType::U8 => "%dl",
                    IrType::I16 | IrType::U16 => "%dx",
                    _ => "%edx",
                };
                emit!(self.state, "    neg{} {}", suffix, reg);
                emit!(self.state, "    lock xadd{} {}, (%ecx)", suffix, reg);
                self.state.emit("    movl %edx, %eax");
            }
            _ => {
                // CAS retry loop for operations without dedicated atomic instructions:
                // And, Or, Xor, Nand, Min, Max, UMin, UMax.
                let suffix = self.type_suffix(ty);
                let edx_reg = match ty {
                    IrType::I8 | IrType::U8 => "%dl",
                    IrType::I16 | IrType::U16 => "%dx",
                    _ => "%edx",
                };
                self.state.emit("    pushl %edx");
                let load_instr = self.mov_load_for_type(ty);
                emit!(self.state, "    {} (%ecx), %eax", load_instr);
                let loop_label = format!(".Latomic_{}", self.state.next_label_id());
                emit!(self.state, "{}:", loop_label);
                self.state.emit("    movl %eax, %edx");
                match op {
                    AtomicRmwOp::And => {
                        emit!(self.state, "    and{} (%esp), {}", suffix, edx_reg);
                    }
                    AtomicRmwOp::Or => {
                        emit!(self.state, "    or{} (%esp), {}", suffix, edx_reg);
                    }
                    AtomicRmwOp::Xor => {
                        emit!(self.state, "    xor{} (%esp), {}", suffix, edx_reg);
                    }
                    AtomicRmwOp::Nand => {
                        emit!(self.state, "    and{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "    not{} {}", suffix, edx_reg);
                    }
                    AtomicRmwOp::Min => {
                        // Signed min: if old <= val, keep old; else new = val
                        let skip_label = format!(".Latomic_skip_{}", self.state.next_label_id());
                        emit!(self.state, "    cmp{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "    jle {}", skip_label);
                        emit!(self.state, "    mov{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "{}:", skip_label);
                    }
                    AtomicRmwOp::Max => {
                        // Signed max: if old >= val, keep old; else new = val
                        let skip_label = format!(".Latomic_skip_{}", self.state.next_label_id());
                        emit!(self.state, "    cmp{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "    jge {}", skip_label);
                        emit!(self.state, "    mov{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "{}:", skip_label);
                    }
                    AtomicRmwOp::UMin => {
                        // Unsigned min: if old <= val (unsigned), keep old; else new = val
                        let skip_label = format!(".Latomic_skip_{}", self.state.next_label_id());
                        emit!(self.state, "    cmp{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "    jbe {}", skip_label);
                        emit!(self.state, "    mov{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "{}:", skip_label);
                    }
                    AtomicRmwOp::UMax => {
                        // Unsigned max: if old >= val (unsigned), keep old; else new = val
                        let skip_label = format!(".Latomic_skip_{}", self.state.next_label_id());
                        emit!(self.state, "    cmp{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "    jae {}", skip_label);
                        emit!(self.state, "    mov{} (%esp), {}", suffix, edx_reg);
                        emit!(self.state, "{}:", skip_label);
                    }
                    _ => {}
                }
                emit!(self.state, "    lock cmpxchg{} {}, (%ecx)", suffix, edx_reg);
                emit!(self.state, "    jne {}", loop_label);
                self.state.emit("    addl $4, %esp");
            }
        }
        self.state.reg_cache.invalidate_acc();
        self.store_eax_to(dest);
    }

    pub(super) fn emit_atomic_cmpxchg_impl(&mut self, dest: &Value, ptr: &Operand, expected: &Operand,
                           desired: &Operand, ty: IrType, _success: AtomicOrdering,
                           _failure: AtomicOrdering, returns_bool: bool) {
        if self.is_atomic_wide(ty) {
            self.emit_atomic_cmpxchg_wide(dest, ptr, expected, desired, returns_bool);
            return;
        }

        self.operand_to_eax(expected);
        self.state.emit("    movl %eax, %edx");
        self.operand_to_eax(desired);
        self.state.emit("    pushl %eax");
        self.esp_adjust += 4;
        self.operand_to_eax(ptr);
        self.state.emit("    movl %eax, %ecx");
        self.state.emit("    movl %edx, %eax");
        self.state.emit("    popl %edx");
        self.esp_adjust -= 4;
        let suffix = self.type_suffix(ty);
        let reg = match ty {
            IrType::I8 | IrType::U8 => "%dl",
            IrType::I16 | IrType::U16 => "%dx",
            _ => "%edx",
        };
        emit!(self.state, "    lock cmpxchg{} {}, (%ecx)", suffix, reg);
        if returns_bool {
            self.state.emit("    sete %al");
            self.state.emit("    movzbl %al, %eax");
        }
        self.state.reg_cache.invalidate_acc();
        self.store_eax_to(dest);
    }

    pub(super) fn emit_atomic_load_impl(&mut self, dest: &Value, ptr: &Operand, ty: IrType, _ordering: AtomicOrdering) {
        if self.is_atomic_wide(ty) {
            self.emit_atomic_load_wide(dest, ptr);
            return;
        }

        self.operand_to_eax(ptr);
        let load_instr = self.mov_load_for_type(ty);
        emit!(self.state, "    {} (%eax), %eax", load_instr);
        self.state.reg_cache.invalidate_acc();
        self.store_eax_to(dest);
    }

    pub(super) fn emit_atomic_store_impl(&mut self, ptr: &Operand, val: &Operand, ty: IrType, ordering: AtomicOrdering) {
        if self.is_atomic_wide(ty) {
            self.emit_atomic_store_wide(ptr, val);
            if matches!(ordering, AtomicOrdering::SeqCst) {
                self.state.emit("    mfence");
            }
            return;
        }

        self.operand_to_eax(val);
        self.state.emit("    movl %eax, %edx");
        self.operand_to_eax(ptr);
        self.state.emit("    movl %eax, %ecx");
        let store_instr = self.mov_store_for_type(ty);
        let reg = match ty {
            IrType::I8 | IrType::U8 => "%dl",
            IrType::I16 | IrType::U16 => "%dx",
            _ => "%edx",
        };
        emit!(self.state, "    {} {}, (%ecx)", store_instr, reg);
        if matches!(ordering, AtomicOrdering::SeqCst) {
            self.state.emit("    mfence");
        }
    }

    pub(super) fn emit_fence_impl(&mut self, ordering: AtomicOrdering) {
        match ordering {
            // Relaxed needs no fence; Consume is treated as Acquire per C11
            // recommendation (§7.17.3p3), and Acquire is a no-op on x86 due to
            // the hardware memory model providing acquire semantics on all loads.
            AtomicOrdering::Relaxed | AtomicOrdering::Consume => {}
            _ => self.state.emit("    mfence"),
        }
    }
}
