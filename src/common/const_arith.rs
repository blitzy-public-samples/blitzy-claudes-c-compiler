//! Shared constant-expression arithmetic helpers.
//!
//! Used by both `sema::const_eval` and `ir::lowering::const_eval` for
//! compile-time constant expression evaluation with proper C semantics.
//!
//! The functions here handle the pure arithmetic: given IrConst operands and
//! width/signedness parameters, they compute the result. The callers (sema and
//! lowering) are responsible for determining width/signedness from their own
//! type systems (CType vs IrType) before calling these shared functions.

use crate::ir::reexports::IrConst;
use crate::frontend::parser::ast::BinOp;

// === Low-level arithmetic primitives ===

/// Wrap an i64 result to 32-bit width if `is_32bit` is true, otherwise return as-is.
/// This handles the C semantics of truncating arithmetic results to `int` width.
#[inline]
fn wrap_result(v: i64, is_32bit: bool) -> i64 {
    if is_32bit { v as i32 as i64 } else { v }
}

/// Perform an unsigned binary operation, handling 32-bit vs 64-bit width.
/// Converts operands to the appropriate unsigned type, applies the operation,
/// and sign-extends the result back to i64.
#[inline]
fn unsigned_op(l: i64, r: i64, is_32bit: bool, op: fn(u64, u64) -> u64) -> i64 {
    if is_32bit {
        op(l as u32 as u64, r as u32 as u64) as u32 as i64
    } else {
        op(l as u64, r as u64) as i64
    }
}

/// Convert a boolean to i64 (1 for true, 0 for false).
#[inline]
fn bool_to_i64(b: bool) -> i64 {
    if b { 1 } else { 0 }
}

// === Shared constant binary operation evaluators ===
//
// These replace the near-identical eval_const_binop / eval_const_binop_float
// implementations that were duplicated between sema::const_eval and
// ir::lowering::const_eval. Both callers now delegate here.

/// Evaluate a constant integer binary operation with proper C width/signedness.
///
/// The caller determines `is_32bit` and `is_unsigned` from their type system:
/// - sema uses CType: `(ctype_size <= 4, ctype.is_unsigned())`
/// - lowering uses IrType: `(ir_type.size() <= 4, ir_type.is_unsigned())`
///
/// Returns `Some(IrConst::I64(result))` or `None` for division by zero.
fn eval_const_binop_int(op: &BinOp, l: i64, r: i64, is_32bit: bool, is_unsigned: bool) -> Option<IrConst> {
    let result = match op {
        BinOp::Add => wrap_result(l.wrapping_add(r), is_32bit),
        BinOp::Sub => wrap_result(l.wrapping_sub(r), is_32bit),
        BinOp::Mul => wrap_result(l.wrapping_mul(r), is_32bit),
        BinOp::Div => {
            if r == 0 { return None; }
            if is_unsigned {
                unsigned_op(l, r, is_32bit, u64::wrapping_div)
            } else {
                wrap_result(l.wrapping_div(r), is_32bit)
            }
        }
        BinOp::Mod => {
            if r == 0 { return None; }
            if is_unsigned {
                unsigned_op(l, r, is_32bit, u64::wrapping_rem)
            } else {
                wrap_result(l.wrapping_rem(r), is_32bit)
            }
        }
        BinOp::BitAnd => l & r,
        BinOp::BitOr => l | r,
        BinOp::BitXor => l ^ r,
        BinOp::Shl => wrap_result(l.wrapping_shl(r as u32), is_32bit),
        BinOp::Shr => {
            if is_unsigned {
                unsigned_op(l, r, is_32bit, |a, b| a.wrapping_shr(b as u32))
            } else if is_32bit {
                (l as i32).wrapping_shr(r as u32) as i64
            } else {
                l.wrapping_shr(r as u32)
            }
        }
        BinOp::Eq => {
            if is_32bit { bool_to_i64(l as u32 == r as u32) }
            else if is_unsigned { bool_to_i64(l as u64 == r as u64) }
            else { bool_to_i64(l == r) }
        }
        BinOp::Ne => {
            if is_32bit { bool_to_i64(l as u32 != r as u32) }
            else if is_unsigned { bool_to_i64(l as u64 != r as u64) }
            else { bool_to_i64(l != r) }
        }
        BinOp::Lt => {
            if is_32bit { if is_unsigned { bool_to_i64((l as u32) < (r as u32)) } else { bool_to_i64((l as i32) < (r as i32)) } }
            else if is_unsigned { bool_to_i64((l as u64) < (r as u64)) }
            else { bool_to_i64(l < r) }
        }
        BinOp::Gt => {
            if is_32bit { if is_unsigned { bool_to_i64((l as u32) > (r as u32)) } else { bool_to_i64((l as i32) > (r as i32)) } }
            else if is_unsigned { bool_to_i64((l as u64) > (r as u64)) }
            else { bool_to_i64(l > r) }
        }
        BinOp::Le => {
            if is_32bit { if is_unsigned { bool_to_i64((l as u32) <= (r as u32)) } else { bool_to_i64((l as i32) <= (r as i32)) } }
            else if is_unsigned { bool_to_i64((l as u64) <= (r as u64)) }
            else { bool_to_i64(l <= r) }
        }
        BinOp::Ge => {
            if is_32bit { if is_unsigned { bool_to_i64((l as u32) >= (r as u32)) } else { bool_to_i64((l as i32) >= (r as i32)) } }
            else if is_unsigned { bool_to_i64((l as u64) >= (r as u64)) }
            else { bool_to_i64(l >= r) }
        }
        BinOp::LogicalAnd => bool_to_i64(l != 0 && r != 0),
        BinOp::LogicalOr => bool_to_i64(l != 0 || r != 0),
    };
    // Preserve the result width so that downstream operations (e.g., division
    // using the result of a shift) can correctly infer the C type. This is
    // critical for expressions like (1 << 31) / N where the shift result must
    // be recognized as 32-bit (INT_MIN = -2147483648) not 64-bit (positive).
    //
    // For unsigned 32-bit results, we must use I64 with zero-extension because
    // IrConst::I32 is signed and cannot correctly represent unsigned values >= 2^31.
    // E.g., (2147483647 * 2U + 1U) = 4294967295U would be stored as I32(-1),
    // which sign-extends to -1 (0xFFFFFFFFFFFFFFFF) when widened to 64-bit.
    // Using I64(4294967295) preserves the correct unsigned value.
    if is_32bit {
        if is_unsigned {
            Some(IrConst::I64(result as u32 as i64))
        } else {
            Some(IrConst::I32(result as i32))
        }
    } else {
        Some(IrConst::I64(result))
    }
}

/// Evaluate a constant floating-point binary operation.
///
/// For LongDouble operands, uses native f128 software arithmetic on the stored
/// IEEE binary128 bytes for full 112-bit mantissa precision. This matches the
/// runtime behavior of __addtf3/__divtf3/etc. on ARM64/RISC-V targets.
/// For F32/F64, uses native Rust arithmetic.
///
/// Comparison and logical operations always return `IrConst::I64`.
pub fn eval_const_binop_float(op: &BinOp, lhs: &IrConst, rhs: &IrConst) -> Option<IrConst> {
    use crate::common::long_double;

    let use_long_double = matches!(lhs, IrConst::LongDouble(..)) || matches!(rhs, IrConst::LongDouble(..));
    let use_f32 = matches!(lhs, IrConst::F32(_)) && matches!(rhs, IrConst::F32(_));

    // For long double arithmetic, use precision matching the target:
    // - ARM64/RISC-V: f128 software arithmetic (full 112-bit mantissa)
    // - x86/i686: x87 80-bit arithmetic (64-bit mantissa)
    if use_long_double {
        if crate::common::types::target_long_double_is_f128() {
            // ARM64/RISC-V: full f128 software arithmetic
            let la = lhs.long_double_bytes().copied().unwrap_or_else(|| {
                let v = lhs.to_f64().unwrap_or(0.0);
                long_double::f64_to_f128_bytes_lossless(v)
            });
            let ra = rhs.long_double_bytes().copied().unwrap_or_else(|| {
                let v = rhs.to_f64().unwrap_or(0.0);
                long_double::f64_to_f128_bytes_lossless(v)
            });

            match op {
                BinOp::Add => {
                    let result = long_double::f128_add(&la, &ra);
                    let approx = long_double::f128_bytes_to_f64(&result);
                    Some(IrConst::long_double_with_bytes(approx, result))
                }
                BinOp::Sub => {
                    let result = long_double::f128_sub(&la, &ra);
                    let approx = long_double::f128_bytes_to_f64(&result);
                    Some(IrConst::long_double_with_bytes(approx, result))
                }
                BinOp::Mul => {
                    let result = long_double::f128_mul(&la, &ra);
                    let approx = long_double::f128_bytes_to_f64(&result);
                    Some(IrConst::long_double_with_bytes(approx, result))
                }
                BinOp::Div => {
                    let result = long_double::f128_div(&la, &ra);
                    let approx = long_double::f128_bytes_to_f64(&result);
                    Some(IrConst::long_double_with_bytes(approx, result))
                }
                BinOp::Mod => {
                    let result = long_double::f128_rem(&la, &ra);
                    let approx = long_double::f128_bytes_to_f64(&result);
                    Some(IrConst::long_double_with_bytes(approx, result))
                }
                BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Gt | BinOp::Le | BinOp::Ge => {
                    let cmp = long_double::f128_cmp(&la, &ra);
                    let result = match op {
                        BinOp::Eq => cmp == 0,
                        BinOp::Ne => cmp != 0,
                        BinOp::Lt => cmp == -1,
                        BinOp::Gt => cmp == 1,
                        BinOp::Le => cmp == -1 || cmp == 0,
                        BinOp::Ge => cmp == 1 || cmp == 0,
                        _ => unreachable!("inner match in const_arith has already-excluded op"),
                    };
                    Some(IrConst::I64(if result { 1 } else { 0 }))
                }
                BinOp::LogicalAnd | BinOp::LogicalOr => {
                    let zero = [0u8; 16];
                    let l_nonzero = long_double::f128_cmp(&la, &zero) != 0;
                    let r_nonzero = long_double::f128_cmp(&ra, &zero) != 0;
                    let result = match op {
                        BinOp::LogicalAnd => l_nonzero && r_nonzero,
                        BinOp::LogicalOr => l_nonzero || r_nonzero,
                        _ => unreachable!("inner match in const_arith has already-excluded op"),
                    };
                    Some(IrConst::I64(if result { 1 } else { 0 }))
                }
                _ => None,
            }
        } else {
            // x86/i686: x87 80-bit arithmetic
            let la = lhs.x87_bytes();
            let ra = rhs.x87_bytes();

            match op {
                BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod => {
                    let result_x87 = match op {
                        BinOp::Add => long_double::x87_add(&la, &ra),
                        BinOp::Sub => long_double::x87_sub(&la, &ra),
                        BinOp::Mul => long_double::x87_mul(&la, &ra),
                        BinOp::Div => long_double::x87_div(&la, &ra),
                        BinOp::Mod => long_double::x87_rem(&la, &ra),
                        _ => unreachable!("inner match in const_arith has already-excluded op"),
                    };
                    let result_f128 = long_double::x87_bytes_to_f128_bytes(&result_x87);
                    let approx = long_double::x87_to_f64(&result_x87);
                    Some(IrConst::long_double_with_bytes(approx, result_f128))
                }
                BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Gt | BinOp::Le | BinOp::Ge => {
                    let l = long_double::x87_to_f64(&la);
                    let r = long_double::x87_to_f64(&ra);
                    let result = match op {
                        BinOp::Eq => l == r,
                        BinOp::Ne => l != r,
                        BinOp::Lt => l < r,
                        BinOp::Gt => l > r,
                        BinOp::Le => l <= r,
                        BinOp::Ge => l >= r,
                        _ => unreachable!("inner match in const_arith has already-excluded op"),
                    };
                    Some(IrConst::I64(if result { 1 } else { 0 }))
                }
                BinOp::LogicalAnd | BinOp::LogicalOr => {
                    let l = long_double::x87_to_f64(&la);
                    let r = long_double::x87_to_f64(&ra);
                    let result = match op {
                        BinOp::LogicalAnd => l != 0.0 && r != 0.0,
                        BinOp::LogicalOr => l != 0.0 || r != 0.0,
                        _ => unreachable!("inner match in const_arith has already-excluded op"),
                    };
                    Some(IrConst::I64(if result { 1 } else { 0 }))
                }
                _ => None,
            }
        }
    } else {
        // F32/F64 path: use native Rust arithmetic
        let l = lhs.to_f64()?;
        let r = rhs.to_f64()?;

        let make_float = |v: f64| -> IrConst {
            if use_f32 {
                IrConst::F32(v as f32)
            } else {
                IrConst::F64(v)
            }
        };

        match op {
            BinOp::Add => Some(make_float(l + r)),
            BinOp::Sub => Some(make_float(l - r)),
            BinOp::Mul => Some(make_float(l * r)),
            BinOp::Div => Some(make_float(l / r)),
            BinOp::Eq => Some(IrConst::I64(if l == r { 1 } else { 0 })),
            BinOp::Ne => Some(IrConst::I64(if l != r { 1 } else { 0 })),
            BinOp::Lt => Some(IrConst::I64(if l < r { 1 } else { 0 })),
            BinOp::Gt => Some(IrConst::I64(if l > r { 1 } else { 0 })),
            BinOp::Le => Some(IrConst::I64(if l <= r { 1 } else { 0 })),
            BinOp::Ge => Some(IrConst::I64(if l >= r { 1 } else { 0 })),
            BinOp::LogicalAnd => Some(IrConst::I64(if l != 0.0 && r != 0.0 { 1 } else { 0 })),
            BinOp::LogicalOr => Some(IrConst::I64(if l != 0.0 || r != 0.0 { 1 } else { 0 })),
            _ => None,
        }
    }
}

/// Evaluate a constant 128-bit integer binary operation.
///
/// Called when at least one operand is I128. Uses native Rust i128/u128 arithmetic
/// to avoid the truncation that occurs when using the i64 path.
///
/// `is_unsigned` is the signedness of the result type (for div/rem/shift/cmp).
/// `lhs_unsigned` and `rhs_unsigned` are the signedness of each operand's original
/// C type, used to correctly zero-extend or sign-extend when widening to i128.
/// Per C11 6.3.1.3, converting unsigned long long to __int128 preserves the value
/// (zero-extends), while converting signed long long sign-extends.
fn eval_const_binop_i128(op: &BinOp, lhs: &IrConst, rhs: &IrConst, is_unsigned: bool, lhs_unsigned: bool, rhs_unsigned: bool) -> Option<IrConst> {
    // When widening a non-I128 operand to 128-bit, use the operand's own signedness
    // to decide zero-extend vs sign-extend. This is critical for cases like:
    //   (i128)x << 64 | 0xFEDCBA9876543210ULL
    // where the RHS is unsigned long long (zero-extend) but the result type is
    // signed __int128.
    let l = if lhs_unsigned && !matches!(lhs, IrConst::I128(_)) {
        // Zero-extend: treat the i64 bit pattern as u64, then widen to u128
        (lhs.to_i64()? as u64 as u128) as i128
    } else {
        lhs.to_i128()?
    };
    let r = if rhs_unsigned && !matches!(rhs, IrConst::I128(_)) {
        (rhs.to_i64()? as u64 as u128) as i128
    } else {
        rhs.to_i128()?
    };

    let bool_result = |b: bool| -> Option<IrConst> {
        Some(IrConst::I64(if b { 1 } else { 0 }))
    };

    match op {
        BinOp::Add => Some(IrConst::I128(l.wrapping_add(r))),
        BinOp::Sub => Some(IrConst::I128(l.wrapping_sub(r))),
        BinOp::Mul => Some(IrConst::I128(l.wrapping_mul(r))),
        BinOp::Div => {
            if r == 0 { return None; }
            if is_unsigned {
                Some(IrConst::I128((l as u128).wrapping_div(r as u128) as i128))
            } else {
                Some(IrConst::I128(l.wrapping_div(r)))
            }
        }
        BinOp::Mod => {
            if r == 0 { return None; }
            if is_unsigned {
                Some(IrConst::I128((l as u128).wrapping_rem(r as u128) as i128))
            } else {
                Some(IrConst::I128(l.wrapping_rem(r)))
            }
        }
        BinOp::BitAnd => Some(IrConst::I128(l & r)),
        BinOp::BitOr => Some(IrConst::I128(l | r)),
        BinOp::BitXor => Some(IrConst::I128(l ^ r)),
        BinOp::Shl => Some(IrConst::I128(l.wrapping_shl(r as u32))),
        BinOp::Shr => {
            if is_unsigned {
                Some(IrConst::I128((l as u128).wrapping_shr(r as u32) as i128))
            } else {
                Some(IrConst::I128(l.wrapping_shr(r as u32)))
            }
        }
        BinOp::Eq => bool_result(l == r),
        BinOp::Ne => bool_result(l != r),
        BinOp::Lt => {
            if is_unsigned { bool_result((l as u128) < (r as u128)) }
            else { bool_result(l < r) }
        }
        BinOp::Gt => {
            if is_unsigned { bool_result((l as u128) > (r as u128)) }
            else { bool_result(l > r) }
        }
        BinOp::Le => {
            if is_unsigned { bool_result((l as u128) <= (r as u128)) }
            else { bool_result(l <= r) }
        }
        BinOp::Ge => {
            if is_unsigned { bool_result((l as u128) >= (r as u128)) }
            else { bool_result(l >= r) }
        }
        BinOp::LogicalAnd => bool_result(l != 0 && r != 0),
        BinOp::LogicalOr => bool_result(l != 0 || r != 0),
    }
}

/// Evaluate a constant binary operation, dispatching to int, i128, or float as needed.
///
/// This is the top-level entry point for constant binary evaluation.
/// The caller provides `is_32bit` and `is_unsigned` for the integer path.
/// `lhs_unsigned` and `rhs_unsigned` indicate the signedness of each operand's
/// original C type, used for correct widening to i128.
pub fn eval_const_binop(op: &BinOp, lhs: &IrConst, rhs: &IrConst, is_32bit: bool, is_unsigned: bool, lhs_unsigned: bool, rhs_unsigned: bool) -> Option<IrConst> {
    let lhs_is_float = matches!(lhs, IrConst::F32(_) | IrConst::F64(_) | IrConst::LongDouble(..));
    let rhs_is_float = matches!(rhs, IrConst::F32(_) | IrConst::F64(_) | IrConst::LongDouble(..));

    if lhs_is_float || rhs_is_float {
        return eval_const_binop_float(op, lhs, rhs);
    }

    // Use native i128 arithmetic when either operand is I128 to avoid truncation.
    if matches!(lhs, IrConst::I128(_)) || matches!(rhs, IrConst::I128(_)) {
        return eval_const_binop_i128(op, lhs, rhs, is_unsigned, lhs_unsigned, rhs_unsigned);
    }

    let l = lhs.to_i64()?;
    let r = rhs.to_i64()?;
    eval_const_binop_int(op, l, r, is_32bit, is_unsigned)
}

/// Negate a constant value (unary `-`).
/// Sub-int types are promoted to i32 per C integer promotion rules.
/// Uses wrapping negation to handle MIN values (e.g. -(-2^63) wraps to -2^63 in C).
pub fn negate_const(val: IrConst) -> Option<IrConst> {
    match val {
        IrConst::I128(v) => Some(IrConst::I128(v.wrapping_neg())),
        IrConst::I64(v) => Some(IrConst::I64(v.wrapping_neg())),
        IrConst::I32(v) => Some(IrConst::I32(v.wrapping_neg())),
        IrConst::I8(v) => Some(IrConst::I32((v as i32).wrapping_neg())),
        IrConst::I16(v) => Some(IrConst::I32((v as i32).wrapping_neg())),
        IrConst::F64(v) => Some(IrConst::F64(-v)),
        IrConst::F32(v) => Some(IrConst::F32(-v)),
        IrConst::LongDouble(v, f128_bytes) => {
            // Negate by flipping the sign bit in the f128 bytes directly,
            // preserving full 112-bit precision
            let val = u128::from_le_bytes(f128_bytes);
            let neg_val = val ^ (1u128 << 127);
            Some(IrConst::long_double_with_bytes(-v, neg_val.to_le_bytes()))
        }
        _ => None,
    }
}

/// Bitwise NOT of a constant value (unary `~`).
/// Sub-int types are promoted to i32 per C integer promotion rules.
pub fn bitnot_const(val: IrConst) -> Option<IrConst> {
    match val {
        IrConst::I128(v) => Some(IrConst::I128(!v)),
        IrConst::I64(v) => Some(IrConst::I64(!v)),
        IrConst::I32(v) => Some(IrConst::I32(!v)),
        IrConst::I8(v) => Some(IrConst::I32(!(v as i32))),
        IrConst::I16(v) => Some(IrConst::I32(!(v as i32))),
        _ => None,
    }
}

/// Check if an AST expression is a zero literal (0 or cast of 0).
/// Used for offsetof pattern detection: `&((type*)0)->member`.
pub fn is_zero_expr(expr: &crate::frontend::parser::ast::Expr) -> bool {
    use crate::frontend::parser::ast::Expr;
    match expr {
        Expr::IntLiteral(0, _) | Expr::UIntLiteral(0, _)
        | Expr::LongLiteral(0, _) | Expr::ULongLiteral(0, _)
        | Expr::LongLongLiteral(0, _) | Expr::ULongLongLiteral(0, _) => true,
        Expr::Cast(_, inner, _) => is_zero_expr(inner),
        _ => false,
    }
}

/// Check if an AST expression is a null pointer constant per C11 6.3.2.3p3:
/// "An integer constant expression with the value 0, or such an expression
///  cast to type void *, is called a null pointer constant."
///
/// This is stricter than is_zero_expr because it requires the expression to be
/// an integer constant expression (ICE), not just evaluating to zero at runtime.
/// For example, `(void*)((long)(volatile_var) * 0l)` is NOT an NPC because
/// `(long)(volatile_var) * 0l` is not an ICE (it involves a volatile variable).
pub fn is_null_pointer_constant(expr: &crate::frontend::parser::ast::Expr) -> bool {
    use crate::frontend::parser::ast::Expr;
    use crate::frontend::parser::ast::TypeSpecifier;
    match expr {
        // Integer literal 0 is an NPC
        Expr::IntLiteral(0, _) | Expr::UIntLiteral(0, _)
        | Expr::LongLiteral(0, _) | Expr::ULongLiteral(0, _)
        | Expr::LongLongLiteral(0, _) | Expr::ULongLongLiteral(0, _) => true,
        // (void*)ICE_zero is an NPC
        Expr::Cast(ts, inner, _) => {
            if let TypeSpecifier::Pointer(p, _) = ts {
                if matches!(p.as_ref(), TypeSpecifier::Void) {
                    // Cast to void*: inner must be an integer constant expression with value 0
                    return is_integer_constant_expr_zero(inner);
                }
            }
            // Cast to non-void-pointer type: still check if it's an NPC
            // (e.g. (int)0 is still an ICE zero)
            is_null_pointer_constant(inner)
        }
        _ => false,
    }
}

/// Check if an expression is an integer constant expression that evaluates to 0.
/// An ICE can only contain literals, sizeof, alignof, enum constants, and operations
/// on these. It cannot contain variable references, function calls, or volatile accesses.
fn is_integer_constant_expr_zero(expr: &crate::frontend::parser::ast::Expr) -> bool {
    // First check if the expression is purely constant (no variable refs)
    if !is_syntactically_constant(expr) {
        return false;
    }
    // Then check if it evaluates to zero
    is_zero_valued_constant(expr)
}

/// Check if an expression is syntactically constant (contains no variable references,
/// function calls, or other non-constant elements). This is a conservative check for
/// integer constant expressions per C11 6.6.
fn is_syntactically_constant(expr: &crate::frontend::parser::ast::Expr) -> bool {
    use crate::frontend::parser::ast::Expr;
    match expr {
        // Literals are constant
        Expr::IntLiteral(_, _) | Expr::UIntLiteral(_, _)
        | Expr::LongLiteral(_, _) | Expr::ULongLiteral(_, _)
        | Expr::LongLongLiteral(_, _) | Expr::ULongLongLiteral(_, _)
        | Expr::CharLiteral(_, _) | Expr::FloatLiteral(_, _)
        | Expr::FloatLiteralF32(_, _) | Expr::FloatLiteralLongDouble(_, _, _) => true,

        // sizeof and alignof are constant (even when applied to expressions)
        Expr::Sizeof(_, _) | Expr::Alignof(_, _)
        | Expr::AlignofExpr(_, _) | Expr::GnuAlignof(_, _)
        | Expr::GnuAlignofExpr(_, _) => true,

        // Casts of constant expressions are constant
        Expr::Cast(_, inner, _) => is_syntactically_constant(inner),

        // Unary ops on constant expressions are constant
        Expr::UnaryOp(_, inner, _) | Expr::PostfixOp(_, inner, _) => {
            is_syntactically_constant(inner)
        }

        // Binary ops on constant expressions are constant
        Expr::BinaryOp(_, lhs, rhs, _) => {
            is_syntactically_constant(lhs) && is_syntactically_constant(rhs)
        }

        // Conditional on constant expressions is constant
        Expr::Conditional(cond, then_e, else_e, _) => {
            is_syntactically_constant(cond) &&
            is_syntactically_constant(then_e) &&
            is_syntactically_constant(else_e)
        }

        // Variable references, function calls, etc. are NOT constant
        // This includes identifiers that might be enum constants, but
        // for the NPC check we're conservative and treat them as non-constant.
        // This is fine because enum-constant-based NPCs would be caught by
        // the literal zero check above.
        _ => false,
    }
}

/// Check if a syntactically-constant expression evaluates to zero.
/// Only call this after is_syntactically_constant returns true.
fn is_zero_valued_constant(expr: &crate::frontend::parser::ast::Expr) -> bool {
    use crate::frontend::parser::ast::Expr;
    match expr {
        Expr::IntLiteral(v, _) => *v == 0,
        Expr::UIntLiteral(v, _) => *v == 0,
        Expr::LongLiteral(v, _) => *v == 0,
        Expr::ULongLiteral(v, _) => *v == 0,
        Expr::LongLongLiteral(v, _) => *v == 0,
        Expr::ULongLongLiteral(v, _) => *v == 0,
        Expr::CharLiteral(v, _) => *v == '\0',
        // Cast of zero is zero
        Expr::Cast(_, inner, _) => is_zero_valued_constant(inner),
        // x * 0 = 0 for any constant x; 0 * x = 0
        Expr::BinaryOp(BinOp::Mul, lhs, rhs, _) => {
            is_zero_valued_constant(lhs) || is_zero_valued_constant(rhs)
        }
        // 0 + 0, 0 - 0, 0 & x, etc. - be conservative, only handle * 0
        // For other ops, we'd need full evaluation which is complex.
        // The * 0 case is the critical one for __is_constexpr.
        _ => false,
    }
}

/// Evaluate raw u64 bit truncation and sign extension for a cast chain.
///
/// Given raw bits from a source value, truncates to `target_width` bits
/// and optionally sign-extends back to 64 bits. Returns `(result_bits, target_signed)`.
///
/// The caller determines `target_width` and `target_signed` from their type system.
pub fn truncate_and_extend_bits(bits: u64, target_width: usize, target_signed: bool) -> (u64, bool) {
    // Truncate to target width
    let truncated = if target_width >= 64 || target_width == 0 {
        bits
    } else {
        bits & ((1u64 << target_width) - 1)
    };

    // If target is signed, sign-extend to 64 bits
    let result = if target_signed && target_width < 64 && target_width > 0 {
        let sign_bit = 1u64 << (target_width - 1);
        if truncated & sign_bit != 0 {
            truncated | !((1u64 << target_width) - 1)
        } else {
            truncated
        }
    } else {
        truncated
    };

    (result, target_signed)
}

// === Complex number constant arithmetic (C11 Annex G) ===
//
// These functions implement compile-time evaluation of _Complex arithmetic
// operations following the rules in C11 Annex G (informative) for handling
// infinities, NaNs, and mixed real/complex operands.
//
// Key Annex G principles:
// - Multiplication and division of complex numbers must produce infinities
//   (not NaN) when mathematically expected, even though the naive algebraic
//   formula may yield NaN due to inf*0 or inf-inf intermediate terms.
// - Division by complex zero produces complex infinity with appropriate signs.
// - Mixed real/complex operations are component-wise (no cross-terms).

/// Evaluate complex multiplication: (a_re + a_im*i) * (b_re + b_im*i)
///
/// Implements the C11 Annex G.5.1 algorithm:
/// 1. Compute the naive result using the standard algebraic formula.
/// 2. If either component of the result is NaN, apply Annex G recovery rules
///    to produce infinity instead of NaN when either operand is infinite.
///
/// The recovery handles three cases:
/// - If operand `a` is infinite: box its components to ±1/±0, recalculate, scale by ∞.
/// - If operand `b` is infinite: same boxing applied to `b`.
/// - If neither operand is infinite but an intermediate product is ±∞, scale result by ∞.
pub fn eval_complex_mul(a_re: f64, a_im: f64, b_re: f64, b_im: f64) -> (f64, f64) {
    // Step 1: Naive formula
    //   result_re = a_re*b_re - a_im*b_im
    //   result_im = a_re*b_im + a_im*b_re
    let ac = a_re * b_re;
    let bd = a_im * b_im;
    let ad = a_re * b_im;
    let bc = a_im * b_re;
    let mut result_re = ac - bd;
    let mut result_im = ad + bc;

    // Step 2: If the naive result contains NaN, attempt Annex G recovery.
    if result_re.is_nan() || result_im.is_nan() {
        let mut ar = a_re;
        let mut ai = a_im;
        let mut br = b_re;
        let mut bi = b_im;

        // Classify whether each operand is "infinite" (has at least one ±∞ component).
        let a_inf = ar.is_infinite() || ai.is_infinite();
        let b_inf = br.is_infinite() || bi.is_infinite();

        // Case 1: Operand `a` is infinite.
        // "Box" its components: infinite → ±1.0, NaN → ±0.0 (preserving sign via copysign).
        // Then replace NaN in b with ±0.0 to prevent inf*NaN propagation.
        if a_inf {
            ar = f64::copysign(if ar.is_infinite() { 1.0 } else { 0.0 }, ar);
            ai = f64::copysign(if ai.is_infinite() { 1.0 } else { 0.0 }, ai);
            if br.is_nan() {
                br = f64::copysign(0.0, br);
            }
            if bi.is_nan() {
                bi = f64::copysign(0.0, bi);
            }
        }

        // Case 2: Operand `b` is infinite.
        // Same boxing logic applied symmetrically to `b`, and NaN cleanup in `a`.
        if b_inf {
            br = f64::copysign(if br.is_infinite() { 1.0 } else { 0.0 }, br);
            bi = f64::copysign(if bi.is_infinite() { 1.0 } else { 0.0 }, bi);
            if ar.is_nan() {
                ar = f64::copysign(0.0, ar);
            }
            if ai.is_nan() {
                ai = f64::copysign(0.0, ai);
            }
        }

        // Case 3: Neither operand is infinite, but an intermediate product overflowed
        // to ±∞. This happens with very large finite operands (e.g. near MAX).
        if !a_inf && !b_inf
            && (ac.is_infinite() || bd.is_infinite() || ad.is_infinite() || bc.is_infinite())
        {
            // Box NaN components of both operands to ±0.0.
            if ar.is_nan() {
                ar = f64::copysign(0.0, ar);
            }
            if ai.is_nan() {
                ai = f64::copysign(0.0, ai);
            }
            if br.is_nan() {
                br = f64::copysign(0.0, br);
            }
            if bi.is_nan() {
                bi = f64::copysign(0.0, bi);
            }
        }

        // Recalculate using boxed values, then scale by infinity.
        // Any of the three cases above ensures that the boxed formula
        // produces a finite result whose sign is correct, and the ∞ scale
        // gives the expected infinite result.
        if a_inf || b_inf
            || ac.is_infinite() || bd.is_infinite()
            || ad.is_infinite() || bc.is_infinite()
        {
            result_re = f64::INFINITY * (ar * br - ai * bi);
            result_im = f64::INFINITY * (ar * bi + ai * br);
        }
    }

    (result_re, result_im)
}

/// Evaluate complex division: (a_re + a_im*i) / (b_re + b_im*i)
///
/// Implements the C11 Annex G.5.2 algorithm:
/// 1. Compute the naive result using the standard algebraic formula with
///    denominator `b_re² + b_im²`.
/// 2. If either component of the result is NaN, apply Annex G recovery:
///    - Finite / zero → ±∞ (with appropriate sign per copysign)
///    - ∞ / finite → ∞ (numerator is infinite, denominator finite)
///    - Finite / ∞ → 0 (numerator finite, denominator infinite)
pub fn eval_complex_div(a_re: f64, a_im: f64, b_re: f64, b_im: f64) -> (f64, f64) {
    // Step 1: Naive formula
    //   denom = b_re² + b_im²
    //   result_re = (a_re*b_re + a_im*b_im) / denom
    //   result_im = (a_im*b_re - a_re*b_im) / denom
    let denom = b_re * b_re + b_im * b_im;
    let mut result_re = (a_re * b_re + a_im * b_im) / denom;
    let mut result_im = (a_im * b_re - a_re * b_im) / denom;

    // Step 2: If the naive result contains NaN, attempt Annex G recovery.
    if result_re.is_nan() || result_im.is_nan() {
        let a_inf = a_re.is_infinite() || a_im.is_infinite();
        let b_inf = b_re.is_infinite() || b_im.is_infinite();
        let b_zero = b_re == 0.0 && b_im == 0.0;

        if b_zero && !a_re.is_nan() && !a_im.is_nan() {
            // Case 1: Divisor is zero, numerator is not NaN.
            // Per C11, complex division by zero produces ±∞ components.
            // Multiply by ∞ to get correct sign propagation from the numerator.
            result_re = f64::INFINITY * a_re;
            result_im = f64::INFINITY * a_im;
        } else if a_inf && !b_inf {
            // Case 2: Numerator is infinite, denominator is finite.
            // Box the numerator: ∞ → ±1.0, finite/NaN → ±0.0.
            let ar = f64::copysign(if a_re.is_infinite() { 1.0 } else { 0.0 }, a_re);
            let ai = f64::copysign(if a_im.is_infinite() { 1.0 } else { 0.0 }, a_im);
            result_re = f64::INFINITY * (ar * b_re + ai * b_im);
            result_im = f64::INFINITY * (ai * b_re - ar * b_im);
        } else if !a_inf && b_inf {
            // Case 3: Numerator is finite, denominator is infinite.
            // Box the denominator: ∞ → ±1.0, finite/NaN → ±0.0.
            // Result converges to zero with correct sign.
            let br = f64::copysign(if b_re.is_infinite() { 1.0 } else { 0.0 }, b_re);
            let bi = f64::copysign(if b_im.is_infinite() { 1.0 } else { 0.0 }, b_im);
            result_re = 0.0 * (a_re * br + a_im * bi);
            result_im = 0.0 * (a_im * br - a_re * bi);
        }
    }

    (result_re, result_im)
}

/// Evaluate mixed real × complex multiplication: real * (c_re + c_im*i)
///
/// Per C11 6.3.1.8 and Annex G, when a real value multiplies a complex value,
/// the real part is treated as (real + 0*i), but the multiplication is done
/// component-wise (no cross-terms with zero imaginary part needed):
///   result = (real * c_re, real * c_im)
///
/// This avoids the inf*0 = NaN pitfall that would occur if we used the full
/// complex multiplication formula with b_im = 0.
pub fn eval_mixed_real_complex_mul(real: f64, c_re: f64, c_im: f64) -> (f64, f64) {
    (real * c_re, real * c_im)
}

/// Evaluate complex / real division: (c_re + c_im*i) / real
///
/// Per C11 Annex G, dividing a complex value by a real scalar is performed
/// component-wise, avoiding the cross-term NaN issues of full complex division:
///   result = (c_re / real, c_im / real)
///
/// Division by zero follows IEEE 754 rules: finite/0 → ±∞, 0/0 → NaN.
pub fn eval_mixed_real_complex_div(c_re: f64, c_im: f64, real: f64) -> (f64, f64) {
    (c_re / real, c_im / real)
}

/// Evaluate real / complex division: real / (c_re + c_im*i)
///
/// Treats the real numerator as (real + 0*i) and applies the conjugate method:
///   real / (c_re + c_im*i) = real * (c_re - c_im*i) / (c_re² + c_im²)
///
/// Delegates to `eval_complex_div` with a_im = 0.0 to get full Annex G
/// recovery for infinities and NaN edge cases. This ensures that:
/// - real / complex_zero → (±∞, ±∞) with correct signs
/// - real / complex_infinity → (0, 0)
/// - Infinity recovery follows the same Annex G.5.2 rules as full complex division
pub fn eval_real_div_complex(real: f64, c_re: f64, c_im: f64) -> (f64, f64) {
    eval_complex_div(real, 0.0, c_re, c_im)
}
