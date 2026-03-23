//! Loop unrolling optimization pass.
//!
//! Fully unrolls loops with compile-time-known constant trip counts when:
//! - The trip count is ≤32 iterations (MAX_UNROLL_ITERATIONS)
//! - The post-unroll instruction count is ≤256 (MAX_UNROLLED_INSTRUCTIONS)
//!
//! This pass is active only at `-O3` optimization level (controlled by the
//! caller in `mod.rs`, not by this pass). It targets simple counted loops
//! of the form:
//!
//!   for (int i = init; i < bound; i += step) { body; }
//!
//! where `init`, `bound`, and `step` are all compile-time constants.
//!
//! The pass uses the shared loop analysis infrastructure from `loop_analysis.rs`
//! (natural loop detection, preheader identification) and follows the same
//! induction variable detection patterns as `iv_strength_reduce.rs`.
//!
//! Loop unrolling eliminates the overhead of branch misprediction, loop counter
//! updates, and comparison instructions for small loops, and exposes more
//! opportunities for subsequent optimization passes (constant folding, DCE, GVN).

use crate::common::fx_hash::{FxHashMap, FxHashSet};
use crate::common::source::Span;
use crate::common::types::IrType;
use crate::ir::analysis;
use crate::ir::reexports::{
    BasicBlock, BlockId, Instruction, IrBinOp, IrCmpOp, IrConst, IrFunction, Operand, Terminator,
    Value,
};
use super::loop_analysis::{self, NaturalLoop};

/// Maximum number of iterations for unrolling eligibility.
const MAX_UNROLL_ITERATIONS: u64 = 32;

/// Maximum number of IR instructions in the fully unrolled body (hard limit).
const MAX_UNROLLED_INSTRUCTIONS: usize = 256;

/// Maximum cast chain length when looking through casts to find root values.
const MAX_CAST_CHAIN_LENGTH: usize = 10;

// ─── Internal data structures ────────────────────────────────────────────────

/// Information about the loop's induction variable.
struct LoopIV {
    /// The phi destination value in the header.
    phi_dest: Value,
    /// Type of the IV.
    ty: IrType,
    /// Initial value (constant) from preheader.
    init_val: i64,
    /// Step value per iteration (constant).
    step: i64,
    /// The Value ID of the IV's "next" value (iv + step) computed in the loop.
    #[allow(dead_code)]
    next_val: Value,
}

/// Information about the loop's exit condition.
struct ExitCondition {
    /// Block index containing the exit CondBranch.
    cond_block: usize,
    /// The comparison operator used (in canonical form: `iv <op> bound`).
    cmp_op: IrCmpOp,
    /// The constant bound value.
    bound: i64,
    /// BlockId of the exit target (outside the loop).
    exit_target: BlockId,
    /// BlockId of the continue target (inside the loop).
    continue_target: BlockId,
    /// Whether the IV is on the LHS of the comparison.
    iv_on_lhs: bool,
    /// Whether the exit is taken on the true branch of the CondBranch.
    /// `true` means the loop exits when the comparison evaluates to true.
    exit_on_true: bool,
}

// ─── Public entry point ──────────────────────────────────────────────────────

/// Run loop unrolling on a single function.
///
/// Returns the number of loops successfully unrolled (0 or 1).
/// Processes only ONE loop per invocation to avoid block-index invalidation.
/// The caller (`mod.rs`) re-invokes if this pass reports changes.
pub(crate) fn unroll_loops(func: &mut IrFunction) -> usize {
    if func.blocks.len() < 2 {
        return 0;
    }

    let cfg = analysis::CfgAnalysis::build(func);
    let loops =
        loop_analysis::find_natural_loops(cfg.num_blocks, &cfg.preds, &cfg.succs, &cfg.idom);
    if loops.is_empty() {
        return 0;
    }

    let mut loops = loop_analysis::merge_loops_by_header(loops);

    // Sort by body size ascending — innermost (smallest) loops first, same
    // strategy as iv_strength_reduce.rs.
    loops.sort_by_key(|l| l.body.len());

    // Try to unroll each loop; return immediately on the first success to
    // avoid stale block-index references.
    for natural_loop in &loops {
        if try_unroll_loop(func, natural_loop, &cfg.preds) {
            return 1;
        }
    }

    0
}

// ─── Loop analysis and candidate detection ───────────────────────────────────

/// Analyze a single natural loop and attempt to unroll it.
/// Returns `true` if the loop was successfully unrolled.
fn try_unroll_loop(
    func: &mut IrFunction,
    natural_loop: &NaturalLoop,
    preds: &analysis::FlatAdj,
) -> bool {
    let header = natural_loop.header;

    // Require a single preheader (one predecessor outside the loop).
    let preheader = match loop_analysis::find_preheader(header, &natural_loop.body, preds) {
        Some(ph) => ph,
        None => return false,
    };

    // Find back-edge blocks (predecessors of header inside the loop body).
    let back_blocks: Vec<usize> = preds
        .row(header)
        .iter()
        .map(|&p| p as usize)
        .filter(|p| natural_loop.body.contains(p))
        .collect();

    // Only handle single-latch loops.
    if back_blocks.len() != 1 {
        return false;
    }
    let latch = back_blocks[0];

    // Detect the induction variable.
    let iv = match find_loop_iv(func, header, &natural_loop.body, preheader, latch) {
        Some(iv) => iv,
        None => return false,
    };

    // Detect the exit condition.
    let exit = match find_exit_condition(func, &natural_loop.body, &iv) {
        Some(e) => e,
        None => return false,
    };

    // Compute the trip count.
    let trip_count = match compute_trip_count(&iv, &exit) {
        Some(tc) => tc,
        None => return false,
    };

    // Count loop body instructions.
    let body_inst_count: usize = natural_loop
        .body
        .iter()
        .filter_map(|&bi| func.blocks.get(bi))
        .map(|b| b.instructions.len())
        .sum();

    // Enforce the hard instruction-count limit.
    if trip_count > 0 && body_inst_count.saturating_mul(trip_count as usize) > MAX_UNROLLED_INSTRUCTIONS
    {
        return false;
    }

    // Execute the unrolling transformation.
    execute_unroll(func, natural_loop, &iv, &exit, trip_count, preheader, latch)
}

// ─── Induction variable detection ────────────────────────────────────────────

/// Find the primary induction variable of the loop.
///
/// Searches header phi nodes for the pattern:
///   %iv = phi(const_init from preheader, %iv_next from latch)
///   %iv_next = add(%iv, const_step)  (possibly through casts)
fn find_loop_iv(
    func: &IrFunction,
    header: usize,
    loop_body: &FxHashSet<usize>,
    preheader: usize,
    latch: usize,
) -> Option<LoopIV> {
    // Build a map: value_id → &Instruction for all definitions in the loop.
    let mut loop_defs: FxHashMap<u32, &Instruction> = FxHashMap::default();
    for &bi in loop_body {
        if bi < func.blocks.len() {
            for inst in &func.blocks[bi].instructions {
                if let Some(dest) = inst.dest() {
                    loop_defs.insert(dest.0, inst);
                }
            }
        }
    }

    let preheader_label = func.blocks[preheader].label;
    let latch_label = func.blocks[latch].label;

    // Scan phi nodes in the header.
    for inst in &func.blocks[header].instructions {
        if let Instruction::Phi {
            dest,
            ty,
            incoming,
        } = inst
        {
            // Only integer IVs.
            if !ty.is_integer() {
                continue;
            }

            // Locate the init operand (from preheader) and the back-edge value
            // (from latch).
            let mut init_op = None;
            let mut back_val = None;

            for (op, block_id) in incoming {
                if *block_id == preheader_label {
                    init_op = Some(*op);
                } else if *block_id == latch_label {
                    if let Operand::Value(v) = op {
                        back_val = Some(*v);
                    }
                }
            }

            // The init operand MUST be a constant for unrolling.
            let init_val = match init_op {
                Some(Operand::Const(c)) => match c.to_i64() {
                    Some(v) => v,
                    None => continue,
                },
                _ => continue,
            };

            let back_val = match back_val {
                Some(v) => v,
                None => continue,
            };

            // Check if back_val = phi_dest + const_step (possibly through casts).
            let add_val_id = look_through_casts(back_val.0, &loop_defs);
            if let Some(Instruction::BinOp {
                op, lhs, rhs, ..
            }) = loop_defs.get(&add_val_id)
            {
                if *op == IrBinOp::Add {
                    let phi_id = dest.0;
                    let lhs_root = match lhs {
                        Operand::Value(v) => look_through_casts(v.0, &loop_defs),
                        _ => u32::MAX,
                    };
                    let rhs_root = match rhs {
                        Operand::Value(v) => look_through_casts(v.0, &loop_defs),
                        _ => u32::MAX,
                    };

                    let step_operand = if lhs_root == phi_id {
                        Some(rhs)
                    } else if rhs_root == phi_id {
                        Some(lhs)
                    } else {
                        None
                    };

                    if let Some(Operand::Const(c)) = step_operand {
                        if let Some(step) = c.to_i64() {
                            return Some(LoopIV {
                                phi_dest: *dest,
                                ty: *ty,
                                init_val,
                                step,
                                next_val: back_val,
                            });
                        }
                    }
                }
            }
        }
    }

    None
}

/// Follow cast/copy chains to find the root value.
///
/// This resolves patterns like `Cast(Cast(iv))` back to `iv` so that the
/// induction variable detection can match through integer promotion casts.
fn look_through_casts(mut val_id: u32, defs: &FxHashMap<u32, &Instruction>) -> u32 {
    for _ in 0..MAX_CAST_CHAIN_LENGTH {
        match defs.get(&val_id) {
            Some(Instruction::Cast {
                src: Operand::Value(v),
                ..
            }) => val_id = v.0,
            Some(Instruction::Copy {
                src: Operand::Value(v),
                ..
            }) => val_id = v.0,
            _ => break,
        }
    }
    val_id
}

// ─── Exit condition detection ────────────────────────────────────────────────

/// Search for the canonical exit pattern in the loop body.
///
/// Looks for a `CondBranch` whose condition is a `Cmp` comparing the IV (or a
/// cast of it) against a constant bound. Only single-exit loops are handled.
fn find_exit_condition(
    func: &IrFunction,
    loop_body: &FxHashSet<usize>,
    iv: &LoopIV,
) -> Option<ExitCondition> {
    // Build a label → index map for loop membership checks.
    let label_to_idx: FxHashMap<BlockId, usize> = func
        .blocks
        .iter()
        .enumerate()
        .map(|(i, b)| (b.label, i))
        .collect();

    // Build loop defs for look_through_casts.
    let mut loop_defs: FxHashMap<u32, &Instruction> = FxHashMap::default();
    for &bi in loop_body {
        if bi < func.blocks.len() {
            for inst in &func.blocks[bi].instructions {
                if let Some(dest) = inst.dest() {
                    loop_defs.insert(dest.0, inst);
                }
            }
        }
    }

    let mut found: Option<ExitCondition> = None;

    for &block_idx in loop_body {
        if block_idx >= func.blocks.len() {
            continue;
        }
        let block = &func.blocks[block_idx];

        if let Terminator::CondBranch {
            cond,
            true_label,
            false_label,
        } = &block.terminator
        {
            let cond_val = match cond {
                Operand::Value(v) => v,
                _ => continue,
            };

            // Determine which branch target is inside/outside the loop.
            let true_idx = label_to_idx.get(true_label).copied();
            let false_idx = label_to_idx.get(false_label).copied();

            let true_in_loop = true_idx.map_or(false, |i| loop_body.contains(&i));
            let false_in_loop = false_idx.map_or(false, |i| loop_body.contains(&i));

            // Need exactly one target inside and one outside.
            if true_in_loop == false_in_loop {
                continue;
            }

            let (exit_target, continue_target, exit_on_true) = if true_in_loop {
                // True branch stays in loop; false branch exits.
                (*false_label, *true_label, false)
            } else {
                // False branch stays in loop; true branch exits.
                (*true_label, *false_label, true)
            };

            // Find the Cmp instruction defining cond_val in this block.
            let cmp_inst = block
                .instructions
                .iter()
                .rev()
                .find(|inst| inst.dest() == Some(*cond_val));

            let cmp_inst = match cmp_inst {
                Some(inst) => inst,
                None => continue,
            };

            if let Instruction::Cmp { op, lhs, rhs, .. } = cmp_inst {
                // Determine which operand is the IV and which is the bound.
                let lhs_is_iv = match lhs {
                    Operand::Value(v) => {
                        let root = look_through_casts(v.0, &loop_defs);
                        root == iv.phi_dest.0
                    }
                    _ => false,
                };
                let rhs_is_iv = match rhs {
                    Operand::Value(v) => {
                        let root = look_through_casts(v.0, &loop_defs);
                        root == iv.phi_dest.0
                    }
                    _ => false,
                };

                // Extract the constant bound from the non-IV side.
                let (bound, iv_on_lhs) = if lhs_is_iv {
                    match rhs {
                        Operand::Const(c) => match c.to_i64() {
                            Some(b) => (b, true),
                            None => continue,
                        },
                        _ => continue,
                    }
                } else if rhs_is_iv {
                    match lhs {
                        Operand::Const(c) => match c.to_i64() {
                            Some(b) => (b, false),
                            None => continue,
                        },
                        _ => continue,
                    }
                } else {
                    continue;
                };

                // Only accept the first matching exit; reject multi-exit loops.
                if found.is_some() {
                    return None;
                }

                found = Some(ExitCondition {
                    cond_block: block_idx,
                    cmp_op: *op,
                    bound,
                    exit_target,
                    continue_target,
                    iv_on_lhs,
                    exit_on_true,
                });
            }
        }
    }

    found
}

// ─── Trip count computation ──────────────────────────────────────────────────

/// Compute the exact trip count from the IV parameters and exit condition.
///
/// Returns `None` when the trip count cannot be determined exactly or exceeds
/// `MAX_UNROLL_ITERATIONS`.
fn compute_trip_count(iv: &LoopIV, exit: &ExitCondition) -> Option<u64> {
    if iv.step == 0 {
        return None; // Infinite loop.
    }

    let init = iv.init_val;
    let step = iv.step;
    let bound = exit.bound;

    // Normalize the comparison so it reads `iv <op> bound`.
    let cmp = if exit.iv_on_lhs {
        exit.cmp_op
    } else {
        flip_cmp_op(exit.cmp_op)
    };

    // Determine the "continue" condition:
    //   exit_on_true  → the cmp result being TRUE exits the loop,
    //                    so the loop continues while NOT cmp.
    //   !exit_on_true → the cmp result being FALSE exits the loop,
    //                    so the loop continues while cmp.
    let continue_cond = if exit.exit_on_true {
        negate_cmp(cmp)
    } else {
        cmp
    };

    let tc = trip_count_for_continue_cond(continue_cond, init, bound, step)?;
    if tc > MAX_UNROLL_ITERATIONS {
        return None;
    }
    Some(tc)
}

/// Compute the trip count assuming the loop continues while `iv <cmp> bound`.
fn trip_count_for_continue_cond(
    cmp: IrCmpOp,
    init: i64,
    bound: i64,
    step: i64,
) -> Option<u64> {
    match cmp {
        // continue while iv < bound (signed, ascending)
        IrCmpOp::Slt => {
            if step <= 0 {
                return None;
            }
            if init >= bound {
                return Some(0);
            }
            let range = (bound - init) as u64;
            let s = step as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv <= bound → same as iv < bound + 1
        IrCmpOp::Sle => {
            if step <= 0 {
                return None;
            }
            if init > bound {
                return Some(0);
            }
            let range = (bound - init + 1) as u64;
            let s = step as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv > bound (signed, descending)
        IrCmpOp::Sgt => {
            if step >= 0 {
                return None;
            }
            if init <= bound {
                return Some(0);
            }
            let range = (init - bound) as u64;
            let s = (-step) as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv >= bound → same as iv > bound - 1
        IrCmpOp::Sge => {
            if step >= 0 {
                return None;
            }
            if init < bound {
                return Some(0);
            }
            let range = (init - bound + 1) as u64;
            let s = (-step) as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv < bound (unsigned, ascending)
        IrCmpOp::Ult => {
            if step <= 0 {
                return None;
            }
            let ui = init as u64;
            let ub = bound as u64;
            if ui >= ub {
                return Some(0);
            }
            let range = ub - ui;
            let s = step as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv <= bound (unsigned)
        IrCmpOp::Ule => {
            if step <= 0 {
                return None;
            }
            let ui = init as u64;
            let ub = bound as u64;
            if ui > ub {
                return Some(0);
            }
            let range = ub - ui + 1;
            let s = step as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv > bound (unsigned, descending)
        IrCmpOp::Ugt => {
            if step >= 0 {
                return None;
            }
            let ui = init as u64;
            let ub = bound as u64;
            if ui <= ub {
                return Some(0);
            }
            let range = ui - ub;
            let s = (-(step as i128)) as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv >= bound (unsigned)
        IrCmpOp::Uge => {
            if step >= 0 {
                return None;
            }
            let ui = init as u64;
            let ub = bound as u64;
            if ui < ub {
                return Some(0);
            }
            let range = ui - ub + 1;
            let s = (-(step as i128)) as u64;
            Some(div_ceil_u64(range, s))
        }
        // continue while iv != bound
        IrCmpOp::Ne => {
            if init == bound {
                return Some(0);
            }
            let diff = bound.wrapping_sub(init);
            if step == 0 {
                return None;
            }
            // Must be exactly divisible.
            if diff % step != 0 {
                return None;
            }
            let tc = diff / step;
            if tc <= 0 {
                return None;
            }
            Some(tc as u64)
        }
        // continue while iv == bound → loop runs at most once
        IrCmpOp::Eq => {
            if init == bound {
                Some(1)
            } else {
                Some(0)
            }
        }
    }
}

/// Integer ceiling division for unsigned values.
fn div_ceil_u64(a: u64, b: u64) -> u64 {
    if b == 0 {
        return 0;
    }
    (a + b - 1) / b
}

/// Flip a comparison operator (swap LHS and RHS semantics).
fn flip_cmp_op(op: IrCmpOp) -> IrCmpOp {
    match op {
        IrCmpOp::Eq => IrCmpOp::Eq,
        IrCmpOp::Ne => IrCmpOp::Ne,
        IrCmpOp::Slt => IrCmpOp::Sgt,
        IrCmpOp::Sle => IrCmpOp::Sge,
        IrCmpOp::Sgt => IrCmpOp::Slt,
        IrCmpOp::Sge => IrCmpOp::Sle,
        IrCmpOp::Ult => IrCmpOp::Ugt,
        IrCmpOp::Ule => IrCmpOp::Uge,
        IrCmpOp::Ugt => IrCmpOp::Ult,
        IrCmpOp::Uge => IrCmpOp::Ule,
    }
}

/// Negate a comparison (logical NOT).
fn negate_cmp(op: IrCmpOp) -> IrCmpOp {
    match op {
        IrCmpOp::Eq => IrCmpOp::Ne,
        IrCmpOp::Ne => IrCmpOp::Eq,
        IrCmpOp::Slt => IrCmpOp::Sge,
        IrCmpOp::Sle => IrCmpOp::Sgt,
        IrCmpOp::Sgt => IrCmpOp::Sle,
        IrCmpOp::Sge => IrCmpOp::Slt,
        IrCmpOp::Ult => IrCmpOp::Uge,
        IrCmpOp::Ule => IrCmpOp::Ugt,
        IrCmpOp::Ugt => IrCmpOp::Ule,
        IrCmpOp::Uge => IrCmpOp::Ult,
    }
}

// ─── Block ordering and phi helpers ──────────────────────────────────────────

/// Collect loop body block indices in BFS order starting from the header.
///
/// Any blocks in the body that are unreachable from the header (within the
/// loop) are appended at the end.
fn collect_loop_blocks_ordered(
    func: &IrFunction,
    header: usize,
    body: &FxHashSet<usize>,
) -> Vec<usize> {
    let mut ordered = Vec::with_capacity(body.len());
    let mut visited = FxHashSet::default();
    let mut queue = std::collections::VecDeque::new();

    queue.push_back(header);
    visited.insert(header);

    while let Some(bi) = queue.pop_front() {
        ordered.push(bi);
        let succ_labels = terminator_successors(&func.blocks[bi].terminator);
        for label in succ_labels {
            if let Some(idx) = func.blocks.iter().position(|b| b.label == label) {
                if body.contains(&idx) && visited.insert(idx) {
                    queue.push_back(idx);
                }
            }
        }
    }

    // Include any body blocks missed by BFS (unreachable within the loop).
    for &bi in body {
        if !visited.contains(&bi) {
            ordered.push(bi);
        }
    }

    ordered
}

/// Return the successor `BlockId`s of a terminator.
fn terminator_successors(term: &Terminator) -> Vec<BlockId> {
    match term {
        Terminator::Return(_) | Terminator::Unreachable => Vec::new(),
        Terminator::Branch(bid) => vec![*bid],
        Terminator::CondBranch {
            true_label,
            false_label,
            ..
        } => vec![*true_label, *false_label],
        Terminator::IndirectBranch {
            possible_targets, ..
        } => possible_targets.clone(),
        Terminator::Switch {
            cases, default, ..
        } => {
            let mut targets: Vec<BlockId> = cases.iter().map(|(_, bid)| *bid).collect();
            targets.push(*default);
            targets
        }
    }
}

/// Cached information about a single phi node in the loop header.
struct HeaderPhi {
    dest: Value,
    #[allow(dead_code)]
    ty: IrType,
    /// The operand flowing in from the preheader (iteration 0 seed).
    preheader_val: Operand,
    /// The operand flowing in from the latch (carried value from previous iter).
    latch_val: Operand,
    /// Whether this phi is the loop induction variable.
    is_iv: bool,
}

/// Extract phi information from the header block for all header-level phis.
fn gather_header_phis(
    func: &IrFunction,
    header: usize,
    preheader: usize,
    latch: usize,
    iv: &LoopIV,
) -> Vec<HeaderPhi> {
    let preheader_label = func.blocks[preheader].label;
    let latch_label = func.blocks[latch].label;
    let mut phis = Vec::new();

    for inst in &func.blocks[header].instructions {
        if let Instruction::Phi {
            dest, ty, incoming, ..
        } = inst
        {
            let mut ph_val: Option<Operand> = None;
            let mut lt_val: Option<Operand> = None;
            for (op, bid) in incoming {
                if *bid == preheader_label {
                    ph_val = Some(op.clone());
                }
                if *bid == latch_label {
                    lt_val = Some(op.clone());
                }
            }
            if let (Some(pv), Some(lv)) = (ph_val, lt_val) {
                phis.push(HeaderPhi {
                    dest: *dest,
                    ty: *ty,
                    preheader_val: pv,
                    latch_val: lv,
                    is_iv: dest.0 == iv.phi_dest.0,
                });
            }
        }
    }

    phis
}

// ─── Core unrolling transformation ───────────────────────────────────────────

/// Execute the actual unrolling transformation.
///
/// Replaces the loop with `trip_count` sequential copies of the body, each
/// with the IV replaced by the corresponding constant value. Returns `true`
/// on success.
fn execute_unroll(
    func: &mut IrFunction,
    natural_loop: &NaturalLoop,
    iv: &LoopIV,
    exit: &ExitCondition,
    trip_count: u64,
    preheader: usize,
    latch: usize,
) -> bool {
    let header = natural_loop.header;
    let tc = trip_count as usize;

    // ── Trip count 0: loop body never executes ──
    if tc == 0 {
        func.blocks[preheader].terminator = Terminator::Branch(exit.exit_target);
        return true;
    }

    // ── Preparation ──
    let loop_blocks = collect_loop_blocks_ordered(func, header, &natural_loop.body);
    let blocks_per_iter = loop_blocks.len();
    let header_label = func.blocks[header].label;
    let loop_block_labels: Vec<BlockId> =
        loop_blocks.iter().map(|&bi| func.blocks[bi].label).collect();
    let header_phis = gather_header_phis(func, header, preheader, latch, iv);

    let mut next_value_id = func.next_value_id;
    let mut next_block_num =
        func.blocks.iter().map(|b| b.label.0).max().unwrap_or(0) + 1;

    // ── Pre-allocate block and value maps for every iteration ──
    let mut all_block_maps: Vec<FxHashMap<BlockId, BlockId>> = Vec::with_capacity(tc);
    for _ in 0..tc {
        let mut bm: FxHashMap<BlockId, BlockId> = FxHashMap::default();
        for &orig_label in &loop_block_labels {
            bm.insert(orig_label, BlockId(next_block_num));
            next_block_num += 1;
        }
        all_block_maps.push(bm);
    }

    let mut all_value_maps: Vec<FxHashMap<u32, u32>> = Vec::with_capacity(tc);
    for _ in 0..tc {
        let mut vm: FxHashMap<u32, u32> = FxHashMap::default();
        for &bi in &loop_blocks {
            for inst in &func.blocks[bi].instructions {
                if let Some(dest) = inst.dest() {
                    vm.insert(dest.0, next_value_id);
                    next_value_id += 1;
                }
            }
        }
        all_value_maps.push(vm);
    }

    // ── Build unrolled blocks ──
    let mut all_new_blocks: Vec<BasicBlock> = Vec::with_capacity(tc * blocks_per_iter);

    for k in 0..tc {
        let value_map = &all_value_maps[k];
        let block_map = &all_block_maps[k];
        let is_last = k == tc - 1;

        for &bi in &loop_blocks {
            let orig = &func.blocks[bi];
            let new_label = block_map[&orig.label];
            let is_header = bi == header;
            let is_cond = bi == exit.cond_block;
            let is_latch = bi == latch;
            let has_spans = !orig.source_spans.is_empty();

            // ── Instructions ──
            let mut new_insts: Vec<Instruction> =
                Vec::with_capacity(orig.instructions.len());
            let mut new_spans: Vec<Span> = Vec::new();

            for (inst_idx, inst) in orig.instructions.iter().enumerate() {
                let span = if has_spans && inst_idx < orig.source_spans.len() {
                    orig.source_spans[inst_idx]
                } else {
                    Span::dummy()
                };

                // Header phis get special treatment.
                if is_header {
                    if let Instruction::Phi { dest, .. } = inst {
                        if let Some(phi) =
                            header_phis.iter().find(|p| p.dest.0 == dest.0)
                        {
                            let new_dest = Value(value_map[&dest.0]);
                            let copy_inst = if phi.is_iv {
                                let iv_val =
                                    iv.init_val + (k as i64) * iv.step;
                                Instruction::Copy {
                                    dest: new_dest,
                                    src: Operand::Const(make_iv_const(
                                        iv_val, iv.ty,
                                    )),
                                }
                            } else {
                                let src = if k == 0 {
                                    phi.preheader_val.clone()
                                } else {
                                    remap_operand(
                                        &phi.latch_val,
                                        &all_value_maps[k - 1],
                                    )
                                };
                                Instruction::Copy {
                                    dest: new_dest,
                                    src,
                                }
                            };
                            new_insts.push(copy_inst);
                            if has_spans {
                                new_spans.push(span);
                            }
                            continue;
                        }
                    }
                }

                // Normal instruction: clone and remap.
                new_insts.push(remap_instruction(inst, value_map, block_map));
                if has_spans {
                    new_spans.push(span);
                }
            }

            // ── Terminator ──
            let new_term = if is_cond && is_latch {
                // Combined exit-check + back-edge block.
                if is_last {
                    Terminator::Branch(exit.exit_target)
                } else {
                    Terminator::Branch(all_block_maps[k + 1][&header_label])
                }
            } else if is_cond {
                // Condition-only block (header, separate from latch/body).
                // For ALL iterations (including the last), the condition is
                // known to pass, so always branch to the body/continue
                // target. The LATCH will handle branching to exit for the
                // last iteration.
                Terminator::Branch(remap_block_id(
                    &exit.continue_target,
                    block_map,
                ))
            } else if is_latch {
                if is_last {
                    // Last iteration latch: if cond_block differs, the exit
                    // already happened upstream, so this block may be
                    // unreachable. Branch to exit for well-formed CFG.
                    Terminator::Branch(exit.exit_target)
                } else {
                    Terminator::Branch(all_block_maps[k + 1][&header_label])
                }
            } else {
                remap_terminator(&orig.terminator, value_map, block_map)
            };

            all_new_blocks.push(BasicBlock {
                label: new_label,
                instructions: new_insts,
                terminator: new_term,
                source_spans: new_spans,
            });
        }
    }

    // ── Redirect preheader ──
    let first_header = all_block_maps[0][&header_label];
    func.blocks[preheader].terminator = Terminator::Branch(first_header);

    // ── Patch exit block phis ──
    // Add incoming entries from the last iteration so values escaping the
    // loop carry the correct SSA definitions.
    let last_vm = &all_value_maps[tc - 1];
    let last_bm = &all_block_maps[tc - 1];
    if let Some(exit_idx) =
        func.blocks.iter().position(|b| b.label == exit.exit_target)
    {
        for inst in &mut func.blocks[exit_idx].instructions {
            if let Instruction::Phi { incoming, .. } = inst {
                let mut extra: Vec<(Operand, BlockId)> = Vec::new();
                for (op, bid) in incoming.iter() {
                    if loop_block_labels.contains(bid) {
                        extra.push((
                            remap_operand(op, last_vm),
                            remap_block_id(bid, last_bm),
                        ));
                    }
                }
                incoming.extend(extra);
            }
        }
    }

    // ── Remap uses of loop-defined values in blocks OUTSIDE the loop ──
    // After unrolling, the original loop blocks become dead (unreachable).
    // However, blocks outside the loop may directly reference values that
    // were defined inside the loop body (without a phi). These references
    // are now stale because the original loop body is dead. We must remap
    // all such uses to point to the last unrolled iteration's definitions.
    //
    // CRITICAL: For header phi values, the exit-time value is the LATCH
    // value from the last iteration (the output of the last body), not the
    // header copy value (the input to the last body). We build an "exit
    // value map" that overrides last_vm for header phi destinations.
    let loop_defined_values: FxHashSet<u32> = {
        let mut s = FxHashSet::default();
        for &bi in &loop_blocks {
            for inst in &func.blocks[bi].instructions {
                if let Some(dest) = inst.dest() {
                    s.insert(dest.0);
                }
            }
        }
        s
    };
    let loop_block_set: FxHashSet<usize> = natural_loop.body.clone();
    let last_vm = &all_value_maps[tc - 1];

    // Build exit value map: start with last_vm, then override header phi
    // destinations to point to the latch value from the last iteration.
    let mut exit_vm: FxHashMap<u32, u32> = last_vm.clone();
    for phi in &header_phis {
        // For each header phi: when the loop exits, the phi value equals
        // the latch operand from the last completed body iteration.
        // Remap the latch operand through last_vm to get the correct ID.
        let remapped_latch = remap_operand(&phi.latch_val, last_vm);
        if let Operand::Value(v) = remapped_latch {
            exit_vm.insert(phi.dest.0, v.0);
        }
        // If latch_val is a constant, no remapping needed for the phi
        // dest — it won't appear as a Value operand in escape uses.
    }

    for (bi, block) in func.blocks.iter_mut().enumerate() {
        if loop_block_set.contains(&bi) {
            continue; // Skip original loop blocks (they are dead)
        }
        // Remap operands in instructions
        for inst in &mut block.instructions {
            remap_loop_escape_uses(inst, &loop_defined_values, &exit_vm);
        }
        // Remap operands in the terminator
        remap_terminator_escape_uses(&mut block.terminator, &loop_defined_values, &exit_vm);
    }

    // ── Finalize ──
    func.blocks.extend(all_new_blocks);
    func.next_value_id = next_value_id;

    true
}

// ─── Value / operand / block remapping ───────────────────────────────────────

/// Remap a `Value` in place, leaving it unchanged if it is not in the map
/// (i.e., defined outside the loop).
#[inline]
fn remap_value_in_place(v: &mut Value, vm: &FxHashMap<u32, u32>) {
    if let Some(&new_id) = vm.get(&v.0) {
        v.0 = new_id;
    }
}

/// Remap an `Operand` in place.
#[inline]
fn remap_operand_in_place(op: &mut Operand, vm: &FxHashMap<u32, u32>) {
    if let Operand::Value(v) = op {
        if let Some(&new_id) = vm.get(&v.0) {
            *v = Value(new_id);
        }
    }
}

/// Return a remapped copy of an `Operand`.
fn remap_operand(op: &Operand, vm: &FxHashMap<u32, u32>) -> Operand {
    match op {
        Operand::Value(v) => {
            if let Some(&new_id) = vm.get(&v.0) {
                Operand::Value(Value(new_id))
            } else {
                Operand::Value(*v)
            }
        }
        Operand::Const(c) => Operand::Const(c.clone()),
    }
}

/// Remap a `BlockId`, returning the original if not in the map.
#[inline]
fn remap_block_id(bid: &BlockId, bm: &FxHashMap<BlockId, BlockId>) -> BlockId {
    bm.get(bid).copied().unwrap_or(*bid)
}

// ─── Instruction remapping ───────────────────────────────────────────────────

/// Clone an instruction and remap all `Value`, `Operand`, and `BlockId`
/// references according to the supplied maps.
fn remap_instruction(
    inst: &Instruction,
    vm: &FxHashMap<u32, u32>,
    bm: &FxHashMap<BlockId, BlockId>,
) -> Instruction {
    let mut cloned = inst.clone();
    remap_instruction_in_place(&mut cloned, vm, bm);
    cloned
}

/// In-place remapping of all value/operand/block references in an
/// instruction.  This covers every `Instruction` variant exhaustively so
/// that new variants added in the future will produce a compile error.
#[allow(clippy::too_many_lines)]
fn remap_instruction_in_place(
    inst: &mut Instruction,
    vm: &FxHashMap<u32, u32>,
    bm: &FxHashMap<BlockId, BlockId>,
) {
    match inst {
        // ── No used values ──
        Instruction::Alloca { dest, .. } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::GlobalAddr { dest, .. } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::Fence { .. } => {}

        // ── Single pointer operand ──
        Instruction::Load { dest, ptr, .. } => {
            remap_value_in_place(dest, vm);
            remap_value_in_place(ptr, vm);
        }
        Instruction::Store { val, ptr, .. } => {
            remap_operand_in_place(val, vm);
            remap_value_in_place(ptr, vm);
        }

        // ── Binary ──
        Instruction::BinOp {
            dest, lhs, rhs, ..
        }
        | Instruction::Cmp {
            dest, lhs, rhs, ..
        } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(lhs, vm);
            remap_operand_in_place(rhs, vm);
        }

        // ── Unary ──
        Instruction::UnaryOp { dest, src, .. }
        | Instruction::Cast { dest, src, .. } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(src, vm);
        }
        Instruction::Copy { dest, src } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(src, vm);
        }

        // ── Calls ──
        Instruction::Call { info, .. } => {
            if let Some(ref mut d) = info.dest {
                remap_value_in_place(d, vm);
            }
            for arg in &mut info.args {
                remap_operand_in_place(arg, vm);
            }
        }
        Instruction::CallIndirect { func_ptr, info } => {
            remap_operand_in_place(func_ptr, vm);
            if let Some(ref mut d) = info.dest {
                remap_value_in_place(d, vm);
            }
            for arg in &mut info.args {
                remap_operand_in_place(arg, vm);
            }
        }

        // ── GEP ──
        Instruction::GetElementPtr {
            dest,
            base,
            offset,
            ..
        } => {
            remap_value_in_place(dest, vm);
            remap_value_in_place(base, vm);
            remap_operand_in_place(offset, vm);
        }

        // ── Memory ──
        Instruction::Memcpy { dest, src, .. } => {
            remap_value_in_place(dest, vm);
            remap_value_in_place(src, vm);
        }
        Instruction::DynAlloca { dest, size, .. } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(size, vm);
        }

        // ── Variadic ──
        Instruction::VaArg {
            dest,
            va_list_ptr,
            ..
        } => {
            remap_value_in_place(dest, vm);
            remap_value_in_place(va_list_ptr, vm);
        }
        Instruction::VaArgStruct {
            dest_ptr,
            va_list_ptr,
            ..
        } => {
            remap_value_in_place(dest_ptr, vm);
            remap_value_in_place(va_list_ptr, vm);
        }
        Instruction::VaStart { va_list_ptr } => {
            remap_value_in_place(va_list_ptr, vm);
        }
        Instruction::VaEnd { va_list_ptr } => {
            remap_value_in_place(va_list_ptr, vm);
        }
        Instruction::VaCopy { dest_ptr, src_ptr } => {
            remap_value_in_place(dest_ptr, vm);
            remap_value_in_place(src_ptr, vm);
        }

        // ── Atomics ──
        Instruction::AtomicRmw {
            dest, ptr, val, ..
        } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(ptr, vm);
            remap_operand_in_place(val, vm);
        }
        Instruction::AtomicCmpxchg {
            dest,
            ptr,
            expected,
            desired,
            ..
        } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(ptr, vm);
            remap_operand_in_place(expected, vm);
            remap_operand_in_place(desired, vm);
        }
        Instruction::AtomicLoad { dest, ptr, .. } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(ptr, vm);
        }
        Instruction::AtomicStore { ptr, val, .. } => {
            remap_operand_in_place(ptr, vm);
            remap_operand_in_place(val, vm);
        }

        // ── Phi ──
        Instruction::Phi {
            dest, incoming, ..
        } => {
            remap_value_in_place(dest, vm);
            for (op, bid) in incoming {
                remap_operand_in_place(op, vm);
                *bid = remap_block_id(bid, bm);
            }
        }

        // ── Label address ──
        Instruction::LabelAddr { dest, label } => {
            remap_value_in_place(dest, vm);
            *label = remap_block_id(label, bm);
        }

        // ── Complex return helpers ──
        Instruction::GetReturnF64Second { dest } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::SetReturnF64Second { src } => {
            remap_operand_in_place(src, vm);
        }
        Instruction::GetReturnF32Second { dest } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::SetReturnF32Second { src } => {
            remap_operand_in_place(src, vm);
        }
        Instruction::GetReturnF128Second { dest } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::SetReturnF128Second { src } => {
            remap_operand_in_place(src, vm);
        }

        // ── Inline assembly ──
        Instruction::InlineAsm {
            outputs,
            inputs,
            goto_labels,
            ..
        } => {
            for (_, ptr, _) in outputs {
                remap_value_in_place(ptr, vm);
            }
            for (_, op, _) in inputs {
                remap_operand_in_place(op, vm);
            }
            for (_, bid) in goto_labels {
                *bid = remap_block_id(bid, bm);
            }
        }

        // ── Intrinsics ──
        Instruction::Intrinsic {
            dest,
            dest_ptr,
            args,
            ..
        } => {
            if let Some(d) = dest {
                remap_value_in_place(d, vm);
            }
            if let Some(p) = dest_ptr {
                remap_value_in_place(p, vm);
            }
            for arg in args {
                remap_operand_in_place(arg, vm);
            }
        }

        // ── Select ──
        Instruction::Select {
            dest,
            cond,
            true_val,
            false_val,
            ..
        } => {
            remap_value_in_place(dest, vm);
            remap_operand_in_place(cond, vm);
            remap_operand_in_place(true_val, vm);
            remap_operand_in_place(false_val, vm);
        }

        // ── Stack save / restore ──
        Instruction::StackSave { dest } => {
            remap_value_in_place(dest, vm);
        }
        Instruction::StackRestore { ptr } => {
            remap_value_in_place(ptr, vm);
        }

        // ── ParamRef ──
        Instruction::ParamRef { dest, .. } => {
            remap_value_in_place(dest, vm);
        }
    }
}

// ─── Terminator remapping ────────────────────────────────────────────────────

/// Clone a terminator and remap all value/block references.
fn remap_terminator(
    term: &Terminator,
    vm: &FxHashMap<u32, u32>,
    bm: &FxHashMap<BlockId, BlockId>,
) -> Terminator {
    let mut cloned = term.clone();
    match &mut cloned {
        Terminator::Return(ref mut op) => {
            if let Some(o) = op {
                remap_operand_in_place(o, vm);
            }
        }
        Terminator::Branch(ref mut bid) => {
            *bid = remap_block_id(bid, bm);
        }
        Terminator::CondBranch {
            cond,
            true_label,
            false_label,
        } => {
            remap_operand_in_place(cond, vm);
            *true_label = remap_block_id(true_label, bm);
            *false_label = remap_block_id(false_label, bm);
        }
        Terminator::IndirectBranch {
            target,
            possible_targets,
        } => {
            remap_operand_in_place(target, vm);
            for bid in possible_targets {
                *bid = remap_block_id(bid, bm);
            }
        }
        Terminator::Switch {
            val,
            cases,
            default,
            ..
        } => {
            remap_operand_in_place(val, vm);
            for (_, bid) in cases {
                *bid = remap_block_id(bid, bm);
            }
            *default = remap_block_id(default, bm);
        }
        Terminator::Unreachable => {}
    }
    cloned
}

// ─── IV constant construction ────────────────────────────────────────────────

/// Create an `IrConst` for the induction variable value at a given iteration.
///
/// Delegates to `IrConst::from_i64` which handles all type variants correctly,
/// including unsigned types stored in their signed representation.
fn make_iv_const(val: i64, ty: IrType) -> IrConst {
    IrConst::from_i64(val, ty)
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// ─── Loop-escape value remapping ─────────────────────────────────────────────

/// Remap any uses of loop-defined values in an instruction that lives
/// OUTSIDE the loop. This handles the case where a value defined inside
/// the (now-dead) original loop body is used directly in a non-loop block
/// without a phi node intermediary.
///
/// Reuses `remap_instruction_in_place` with a value map that only includes
/// loop-defined values (so values defined outside the loop are left alone),
/// and an empty block map (block references in non-loop blocks don't need
/// remapping).
///
/// In well-formed SSA, the dest of a non-loop instruction is never in
/// loop_defined, so the restricted_vm will not remap it — only operands
/// (uses) of loop-defined values will be updated.
fn remap_loop_escape_uses(
    inst: &mut Instruction,
    loop_defined: &FxHashSet<u32>,
    last_vm: &FxHashMap<u32, u32>,
) {
    // Build a restricted value map: only remap values that were defined in
    // the original loop body AND that appear in the last iteration's map.
    let restricted_vm: FxHashMap<u32, u32> = last_vm
        .iter()
        .filter(|(k, _)| loop_defined.contains(k))
        .map(|(&k, &v)| (k, v))
        .collect();
    if restricted_vm.is_empty() {
        return;
    }
    let empty_bm: FxHashMap<BlockId, BlockId> = FxHashMap::default();
    remap_instruction_in_place(inst, &restricted_vm, &empty_bm);
}

/// Remap loop-defined value uses in a terminator instruction.
fn remap_terminator_escape_uses(
    term: &mut Terminator,
    loop_defined: &FxHashSet<u32>,
    last_vm: &FxHashMap<u32, u32>,
) {
    let remap_escape_op = |op: &mut Operand| {
        if let Operand::Value(ref mut v) = op {
            if loop_defined.contains(&v.0) {
                if let Some(&new_id) = last_vm.get(&v.0) {
                    *v = Value(new_id);
                }
            }
        }
    };
    match term {
        Terminator::Return(Some(ref mut op)) => { remap_escape_op(op); }
        Terminator::CondBranch { ref mut cond, .. } => { remap_escape_op(cond); }
        Terminator::Switch { ref mut val, .. } => { remap_escape_op(val); }
        Terminator::IndirectBranch { ref mut target, .. } => { remap_escape_op(target); }
        Terminator::Return(None)
        | Terminator::Branch(_)
        | Terminator::Unreachable => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── Trip count computation tests ──

    #[test]
    fn test_trip_count_ascending_slt() {
        // for (i = 0; i < 10; i++)  → trip_count = 10
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 10,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false, // exit when NOT (i < 10) → exit when i >= 10
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(10));
    }

    #[test]
    fn test_trip_count_ascending_sle() {
        // for (i = 0; i <= 9; i++)  → trip_count = 10
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Sle,
            bound: 9,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(10));
    }

    #[test]
    fn test_trip_count_descending() {
        // for (i = 10; i > 0; i--)  → trip_count = 10
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 10,
            step: -1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Sgt,
            bound: 0,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(10));
    }

    #[test]
    fn test_trip_count_ne() {
        // for (i = 0; i != 8; i += 2)  → trip_count = 4
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 2,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Ne,
            bound: 8,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(4));
    }

    #[test]
    fn test_trip_count_zero() {
        // for (i = 10; i < 5; i++)  → trip_count = 0
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 10,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 5,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(0));
    }

    #[test]
    fn test_trip_count_too_large() {
        // for (i = 0; i < 100; i++)  → exceeds limit
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 100,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), None);
    }

    #[test]
    fn test_trip_count_zero_step() {
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 0,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 10,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), None);
    }

    #[test]
    fn test_trip_count_step_two() {
        // for (i = 0; i < 10; i += 2)  → trip_count = 5
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 2,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 10,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(5));
    }

    #[test]
    fn test_trip_count_ceil_division() {
        // for (i = 0; i < 7; i += 3)  → trip_count = ceil(7/3) = 3
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 3,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Slt,
            bound: 7,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(3));
    }

    #[test]
    fn test_trip_count_iv_on_rhs() {
        // bound > iv  → flipped to iv < bound → ascending with step > 0
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Sgt, // bound > iv  →  iv < bound
            bound: 4,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: false,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(4));
    }

    #[test]
    fn test_trip_count_exit_on_true() {
        // exit when i >= 4 (exit_on_true=true, cmp=Sge)
        // → continue while NOT(i >= 4) → continue while i < 4
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::I32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Sge,
            bound: 4,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: true,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(4));
    }

    #[test]
    fn test_trip_count_unsigned_ult() {
        // for (unsigned i = 0; i < 10u; i++)
        let iv = LoopIV {
            phi_dest: Value(0),
            ty: IrType::U32,
            init_val: 0,
            step: 1,
            next_val: Value(1),
        };
        let exit = ExitCondition {
            cond_block: 0,
            cmp_op: IrCmpOp::Ult,
            bound: 10,
            exit_target: BlockId(99),
            continue_target: BlockId(1),
            iv_on_lhs: true,
            exit_on_true: false,
        };
        assert_eq!(compute_trip_count(&iv, &exit), Some(10));
    }

    // ── Helper function tests ──

    #[test]
    fn test_flip_cmp() {
        assert_eq!(flip_cmp_op(IrCmpOp::Slt), IrCmpOp::Sgt);
        assert_eq!(flip_cmp_op(IrCmpOp::Sgt), IrCmpOp::Slt);
        assert_eq!(flip_cmp_op(IrCmpOp::Eq), IrCmpOp::Eq);
        assert_eq!(flip_cmp_op(IrCmpOp::Ne), IrCmpOp::Ne);
    }

    #[test]
    fn test_negate_cmp() {
        assert_eq!(negate_cmp(IrCmpOp::Slt), IrCmpOp::Sge);
        assert_eq!(negate_cmp(IrCmpOp::Sge), IrCmpOp::Slt);
        assert_eq!(negate_cmp(IrCmpOp::Eq), IrCmpOp::Ne);
        assert_eq!(negate_cmp(IrCmpOp::Ne), IrCmpOp::Eq);
    }

    #[test]
    fn test_make_iv_const_i32() {
        let c = make_iv_const(42, IrType::I32);
        assert_eq!(c.to_i64(), Some(42));
    }

    #[test]
    fn test_make_iv_const_i64() {
        let c = make_iv_const(100, IrType::I64);
        assert_eq!(c.to_i64(), Some(100));
    }

    #[test]
    fn test_make_iv_const_u32() {
        let c = make_iv_const(7, IrType::U32);
        assert_eq!(c.to_i64(), Some(7));
    }

    #[test]
    fn test_remap_operand_value() {
        let mut vm = FxHashMap::default();
        vm.insert(5, 10);
        let op = Operand::Value(Value(5));
        let remapped = remap_operand(&op, &vm);
        match remapped {
            Operand::Value(v) => assert_eq!(v.0, 10),
            _ => panic!("expected Value"),
        }
    }

    #[test]
    fn test_remap_operand_const() {
        let vm = FxHashMap::default();
        let op = Operand::Const(IrConst::I32(42));
        let remapped = remap_operand(&op, &vm);
        match remapped {
            Operand::Const(c) => assert_eq!(c.to_i64(), Some(42)),
            _ => panic!("expected Const"),
        }
    }

    #[test]
    fn test_remap_operand_unmapped_value() {
        let vm = FxHashMap::default();
        let op = Operand::Value(Value(99));
        let remapped = remap_operand(&op, &vm);
        match remapped {
            Operand::Value(v) => assert_eq!(v.0, 99),
            _ => panic!("expected Value"),
        }
    }

    #[test]
    fn test_remap_block_id() {
        let mut bm: FxHashMap<BlockId, BlockId> = FxHashMap::default();
        bm.insert(BlockId(1), BlockId(100));
        assert_eq!(remap_block_id(&BlockId(1), &bm), BlockId(100));
        assert_eq!(remap_block_id(&BlockId(2), &bm), BlockId(2));
    }

    #[test]
    fn test_div_ceil() {
        assert_eq!(div_ceil_u64(10, 3), 4);
        assert_eq!(div_ceil_u64(9, 3), 3);
        assert_eq!(div_ceil_u64(0, 5), 0);
        assert_eq!(div_ceil_u64(1, 1), 1);
        assert_eq!(div_ceil_u64(7, 2), 4);
    }

    /// Create a minimal `IrFunction` suitable for testing.
    fn make_test_func(blocks: Vec<BasicBlock>, next_value_id: u32) -> IrFunction {
        IrFunction {
            name: "test".to_string(),
            return_type: IrType::Void,
            params: vec![],
            blocks,
            is_variadic: false,
            is_declaration: false,
            is_static: false,
            is_inline: false,
            is_always_inline: false,
            is_noinline: false,
            next_value_id,
            section: None,
            visibility: None,
            is_weak: false,
            is_used: false,
            is_fastcall: false,
            is_naked: false,
            has_inlined_calls: false,
            param_alloca_values: vec![],
            uses_sret: false,
            global_init_label_blocks: vec![],
            ret_eightbyte_classes: vec![],
            is_gnu_inline_def: false,
        }
    }

    #[test]
    fn test_unroll_no_blocks() {
        // Function with a single block → no loops.
        let mut func = make_test_func(
            vec![BasicBlock {
                label: BlockId(0),
                instructions: vec![],
                terminator: Terminator::Return(None),
                source_spans: vec![],
            }],
            0,
        );
        assert_eq!(unroll_loops(&mut func), 0);
    }
}
