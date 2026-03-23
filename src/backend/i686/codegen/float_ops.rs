//! I686Codegen: Float operation helpers (F128 negation, F64 parameter store).
//!
//! The F64 parameter store helper addresses fix_i686_double_param_high_word_store:
//! on i686, double parameters require two 32-bit movl stores for both low and
//! high words. Missing the high word leaves upper bits uninitialized.
//!
//! ## Background
//!
//! When a function takes a `double` (F64) parameter on i686, the calling
//! convention passes the 64-bit value on the stack as two consecutive 32-bit
//! words (low word at the lower address, high word at +4). The codegen copies
//! this parameter into a local alloca, which also requires two 32-bit `movl`
//! stores. If only the low word is copied, the upper 32 bits of the double
//! remain uninitialized/garbage, causing incorrect computation.
//!
//! The `emit_f64_param_store_impl` helper centralizes the two-word copy pattern
//! so that all call sites (prologue parameter stores, param ref fallback paths)
//! can delegate to a single correct implementation.

use crate::ir::reexports::{Operand, Value};
use crate::emit;
use super::emit::I686Codegen;

impl I686Codegen {
    /// Emit a two-word (64-bit) copy for a double (F64) parameter on i686.
    ///
    /// On i686, a double occupies 8 bytes and must be copied as two separate
    /// 32-bit `movl` instructions — one for the low word and one for the high
    /// word. This helper ensures both words are always emitted, preventing the
    /// bug where only the low word is stored and the high 32 bits are left as
    /// uninitialized stack garbage.
    ///
    /// # Arguments
    ///
    /// * `src_ref` — Assembly operand string for the low 32 bits of the source
    ///   (e.g., `"8(%ebp)"` for a stack parameter).
    /// * `src_ref_hi` — Assembly operand string for the high 32 bits of the
    ///   source (e.g., `"12(%ebp)"`, i.e., source + 4).
    /// * `dst_ref` — Assembly operand string for the low 32 bits of the
    ///   destination slot (e.g., `"-8(%ebp)"`).
    /// * `dst_ref_hi` — Assembly operand string for the high 32 bits of the
    ///   destination slot (e.g., `"-4(%ebp)"`, i.e., dest + 4).
    ///
    /// # Emitted Assembly
    ///
    /// ```text
    /// movl <src_ref>, %eax        # load low word
    /// movl %eax, <dst_ref>        # store low word
    /// movl <src_ref_hi>, %eax     # load high word
    /// movl %eax, <dst_ref_hi>     # store high word
    /// ```
    ///
    /// This addresses `fix_i686_double_param_high_word_store`.
    pub(super) fn emit_f64_param_store_impl(
        &mut self,
        src_ref: &str,
        src_ref_hi: &str,
        dst_ref: &str,
        dst_ref_hi: &str,
    ) {
        // Invalidate the accumulator cache since we use %eax as a transfer
        // register below. If %eax held a cached value, the movl instructions
        // would silently clobber it.
        self.state.reg_cache.invalidate_acc();
        // Copy low 32 bits (bits [31:0])
        emit!(self.state, "    movl {}, %eax", src_ref);
        emit!(self.state, "    movl %eax, {}", dst_ref);
        // Copy high 32 bits (bits [63:32]) — this is the critical part that
        // must not be omitted; without it the upper half of the double is
        // uninitialized, causing incorrect floating-point results.
        emit!(self.state, "    movl {}, %eax", src_ref_hi);
        emit!(self.state, "    movl %eax, {}", dst_ref_hi);
    }

    pub(super) fn emit_f128_neg_impl(&mut self, dest: &Value, src: &Operand) {
        self.emit_f128_load_to_x87(src);
        self.state.emit("    fchs");
        if let Some(slot) = self.state.get_slot(dest.0) {
            let sr = self.slot_ref(slot);
            emit!(self.state, "    fstpt {}", sr);
            self.state.f128_direct_slots.insert(dest.0);
        }
        self.state.reg_cache.invalidate_acc();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Verify that emit_f64_param_store_impl emits BOTH low-word and high-word
    /// movl instructions for a 64-bit double parameter copy. This is the core
    /// correctness check for fix_i686_double_param_high_word_store.
    #[test]
    fn test_emit_f64_param_store_emits_both_words() {
        let mut codegen = I686Codegen::new();

        codegen.emit_f64_param_store_impl(
            "8(%ebp)",   // source low word
            "12(%ebp)",  // source high word
            "-8(%ebp)",  // dest low word
            "-4(%ebp)",  // dest high word
        );

        let output = &codegen.state.out.buf;

        // Verify low-word copy (bits [31:0])
        assert!(
            output.contains("movl 8(%ebp), %eax"),
            "Missing low word load: {}",
            output
        );
        assert!(
            output.contains("movl %eax, -8(%ebp)"),
            "Missing low word store: {}",
            output
        );

        // Verify high-word copy (bits [63:32]) — this is the critical part
        // that the bug fix addresses
        assert!(
            output.contains("movl 12(%ebp), %eax"),
            "Missing high word load: {}",
            output
        );
        assert!(
            output.contains("movl %eax, -4(%ebp)"),
            "Missing high word store: {}",
            output
        );
    }

    /// Verify that exactly four movl instructions are emitted (no extras).
    #[test]
    fn test_emit_f64_param_store_exact_instruction_count() {
        let mut codegen = I686Codegen::new();

        codegen.emit_f64_param_store_impl(
            "16(%ebp)",
            "20(%ebp)",
            "-16(%ebp)",
            "-12(%ebp)",
        );

        let output = &codegen.state.out.buf;
        let movl_count = output.matches("movl").count();
        assert_eq!(
            movl_count, 4,
            "Expected exactly 4 movl instructions, got {}: {}",
            movl_count, output
        );
    }
}
