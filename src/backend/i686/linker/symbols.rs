//! Symbol resolution for the i686 linker.
//!
//! Phases 6-9: global symbol resolution, COMMON symbol allocation,
//! PLT/GOT marking, undefined symbol checking, PLT/GOT list building,
//! and IFUNC collection.

use std::collections::HashMap;

use super::types::*;
use crate::backend::linker_common;

pub(super) fn resolve_symbols(
    inputs: &[InputObject],
    _output_sections: &[OutputSection],
    section_map: &SectionMap,
    dynlib_syms: &HashMap<String, (String, u8, u32, Option<String>, bool, u8)>,
) -> (HashMap<String, LinkerSymbol>, HashMap<(usize, usize), String>) {
    let mut global_symbols: HashMap<String, LinkerSymbol> = HashMap::new();
    let mut sym_resolution: HashMap<(usize, usize), String> = HashMap::new();

    // First pass: collect definitions
    for (obj_idx, obj) in inputs.iter().enumerate() {
        for (sym_idx, sym) in obj.symbols.iter().enumerate() {
            if sym.name.is_empty() || sym.sym_type == STT_FILE || sym.sym_type == STT_SECTION {
                continue;
            }
            if sym.section_index == SHN_UNDEF { continue; }

            let (out_sec_idx, sec_offset) = if sym.section_index != SHN_ABS && sym.section_index != SHN_COMMON {
                section_map.get(&(obj_idx, sym.section_index as usize))
                    .copied().unwrap_or((usize::MAX, 0))
            } else {
                (usize::MAX, 0)
            };

            let new_sym = LinkerSymbol {
                address: 0,
                size: sym.size,
                sym_type: sym.sym_type,
                binding: sym.binding,
                visibility: sym.visibility,
                is_defined: true,
                needs_plt: false,
                needs_got: false,
                output_section: out_sec_idx,
                section_offset: sec_offset + sym.value,
                plt_index: 0,
                got_index: 0,
                is_dynamic: false,
                dynlib: String::new(),
                needs_copy: false,
                copy_addr: 0,
                version: None,
                uses_textrel: false,
            };

            match global_symbols.get(&sym.name) {
                None => {
                    // Note: STB_LOCAL symbols are deliberately inserted here when no
                    // entry exists yet. Unlike ELF64 backends that skip locals entirely,
                    // the i686 backend must allow locals as fallback definitions because
                    // glibc's static archives contain cross-object references that resolve
                    // through local symbols. The Some arm below prevents locals from
                    // *overriding* any existing entry (global, weak, or other local).
                    global_symbols.insert(sym.name.clone(), new_sym);
                }
                Some(existing) => {
                    // Local symbols must not override any existing entry.
                    // They have file scope only and should not shadow globals
                    // or weaks from other objects (e.g. a static "data" in one
                    // file must not shadow a global "data" in another).
                    if sym.binding == STB_LOCAL {
                        // Already have a definition; keep it.
                    } else if sym.binding == STB_GLOBAL && (existing.binding == STB_WEAK || existing.binding == STB_LOCAL)
                        || (!existing.is_defined && new_sym.is_defined)
                    {
                        global_symbols.insert(sym.name.clone(), new_sym);
                    }
                }
            }

            sym_resolution.insert((obj_idx, sym_idx), sym.name.clone());
        }
    }

    // Second pass: resolve undefined references against dynamic libraries
    for (obj_idx, obj) in inputs.iter().enumerate() {
        for (sym_idx, sym) in obj.symbols.iter().enumerate() {
            if sym.name.is_empty() || sym.sym_type == STT_FILE { continue; }
            sym_resolution.insert((obj_idx, sym_idx), sym.name.clone());

            if sym.section_index == SHN_UNDEF {
                if global_symbols.contains_key(&sym.name) { continue; }

                if let Some((lib, dyn_sym_type, dyn_size, dyn_ver, _is_default, dyn_binding)) = dynlib_syms.get(&sym.name) {
                    let is_func = *dyn_sym_type == STT_FUNC || *dyn_sym_type == STT_GNU_IFUNC;
                    global_symbols.insert(sym.name.clone(), LinkerSymbol {
                        address: 0,
                        size: *dyn_size,
                        sym_type: *dyn_sym_type,
                        binding: *dyn_binding,
                        visibility: STV_DEFAULT,
                        is_defined: false,
                        needs_plt: is_func,
                        needs_got: is_func,
                        output_section: usize::MAX,
                        section_offset: 0,
                        plt_index: 0,
                        got_index: 0,
                        is_dynamic: true,
                        dynlib: lib.clone(),
                        needs_copy: !is_func,
                        copy_addr: 0,
                        version: dyn_ver.clone(),
                        uses_textrel: false,
                    });
                } else {
                    global_symbols.entry(sym.name.clone()).or_insert(LinkerSymbol {
                        address: 0,
                        size: 0,
                        sym_type: sym.sym_type,
                        binding: sym.binding,
                        visibility: STV_DEFAULT,
                        is_defined: false,
                        needs_plt: false,
                        needs_got: false,
                        output_section: usize::MAX,
                        section_offset: 0,
                        plt_index: 0,
                        got_index: 0,
                        is_dynamic: false,
                        dynlib: String::new(),
                        needs_copy: false,
                        copy_addr: 0,
                        version: None,
                        uses_textrel: false,
                    });
                }
            }
        }
    }

    // Resolve section symbols
    for (obj_idx, obj) in inputs.iter().enumerate() {
        for (sym_idx, sym) in obj.symbols.iter().enumerate() {
            if sym.sym_type == STT_SECTION && sym.section_index != SHN_UNDEF {
                sym_resolution.insert((obj_idx, sym_idx),
                    format!("__section_{}_{}", obj_idx, sym.section_index));
            }
        }
    }

    (global_symbols, sym_resolution)
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 6b: Allocate COMMON symbols in .bss
// ══════════════════════════════════════════════════════════════════════════════

/// Allocate COMMON symbols (tentative definitions) in the .bss section.
///
/// In C, a global variable declared without an initializer (e.g. `int x;`) may be
/// emitted as a COMMON symbol (SHN_COMMON) by the compiler. These symbols need
/// space allocated in .bss during linking. For each COMMON symbol, we:
/// 1. Find or create the .bss output section
/// 2. Align the current offset to the symbol's alignment requirement
/// 3. Update the LinkerSymbol to point into the .bss section
pub(super) fn allocate_common_symbols(
    inputs: &[InputObject],
    output_sections: &mut Vec<OutputSection>,
    section_name_to_idx: &mut HashMap<String, usize>,
    global_symbols: &mut HashMap<String, LinkerSymbol>,
) {
    // Collect COMMON symbols: (name, alignment, size)
    // For COMMON symbols, InputSymbol.value is the alignment requirement, .size is the size.
    let mut common_syms: Vec<(String, u32, u32)> = Vec::new();
    for obj in inputs.iter() {
        for sym in &obj.symbols {
            if sym.section_index == SHN_COMMON && !sym.name.is_empty() {
                // Only add if this symbol is still in global_symbols with output_section == usize::MAX
                // (i.e., it wasn't overridden by a real definition from another object)
                if let Some(gs) = global_symbols.get(&sym.name) {
                    if gs.output_section == usize::MAX && gs.is_defined && !gs.is_dynamic {
                        // Check we haven't already added this symbol (could appear in multiple objects)
                        if !common_syms.iter().any(|(n, _, _)| n == &sym.name) {
                            common_syms.push((sym.name.clone(), sym.value.max(1), sym.size));
                        }
                    }
                }
            }
        }
    }

    if common_syms.is_empty() { return; }

    // Find or create .bss section
    let bss_idx = if let Some(&idx) = section_name_to_idx.get(".bss") {
        idx
    } else {
        let idx = output_sections.len();
        output_sections.push(OutputSection {
            name: ".bss".to_string(),
            sh_type: SHT_NOBITS,
            flags: SHF_ALLOC | SHF_WRITE,
            data: Vec::new(),
            align: 4,
            addr: 0,
            file_offset: 0,
        });
        section_name_to_idx.insert(".bss".to_string(), idx);
        idx
    };

    let mut bss_off = output_sections[bss_idx].data.len() as u32;
    for (name, alignment, size) in &common_syms {
        let a = (*alignment).max(1);
        bss_off = (bss_off + a - 1) & !(a - 1);

        if let Some(sym) = global_symbols.get_mut(name) {
            sym.output_section = bss_idx;
            sym.section_offset = bss_off;
        }

        if *alignment > output_sections[bss_idx].align {
            output_sections[bss_idx].align = *alignment;
        }
        bss_off += size;
    }

    // Extend .bss data to reflect the new size
    let new_len = bss_off as usize;
    if new_len > output_sections[bss_idx].data.len() {
        output_sections[bss_idx].data.resize(new_len, 0);
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 7: PLT/GOT marking + undefined check
// ══════════════════════════════════════════════════════════════════════════════

pub(super) fn mark_plt_got_needs(
    inputs: &[InputObject],
    global_symbols: &mut HashMap<String, LinkerSymbol>,
    _is_static: bool,
) {
    for obj in inputs.iter() {
        for sec in &obj.sections {
            for &(_, rel_type, sym_idx, _) in &sec.relocations {
                let sym = if (sym_idx as usize) < obj.symbols.len() {
                    &obj.symbols[sym_idx as usize]
                } else { continue; };

                if sym.sym_type == STT_SECTION || sym.name.is_empty() { continue; }

                match rel_type {
                    R_386_PLT32 => {
                        if let Some(gs) = global_symbols.get_mut(&sym.name) {
                            if gs.is_dynamic {
                                gs.needs_plt = true;
                                gs.needs_got = true;
                            }
                        }
                    }
                    R_386_GOT32 | R_386_GOT32X => {
                        if let Some(gs) = global_symbols.get_mut(&sym.name) {
                            gs.needs_got = true;
                        }
                    }
                    R_386_TLS_GOTIE | R_386_TLS_IE => {
                        if let Some(gs) = global_symbols.get_mut(&sym.name) {
                            gs.needs_got = true;
                        }
                    }
                    _ => {}
                }
            }
        }
    }
}

pub(super) fn check_undefined_symbols(global_symbols: &HashMap<String, LinkerSymbol>) -> Result<(), String> {
    let truly_undefined: Vec<&String> = global_symbols.iter()
        .filter(|(n, s)| !s.is_defined && !s.is_dynamic && s.binding != STB_WEAK
            && !linker_common::is_linker_defined_symbol(n))
        .map(|(n, _)| n)
        .collect();

    if !truly_undefined.is_empty() {
        return Err(format!("undefined symbols: {}", truly_undefined.iter()
            .map(|s| s.as_str()).collect::<Vec<_>>().join(", ")));
    }
    Ok(())
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 8: PLT/GOT list building
// ══════════════════════════════════════════════════════════════════════════════

pub(super) fn build_plt_got_lists(
    global_symbols: &mut HashMap<String, LinkerSymbol>,
) -> (Vec<String>, Vec<String>, Vec<String>, usize, usize) {
    let mut plt_symbols: Vec<String> = Vec::new();
    let mut got_dyn_symbols: Vec<String> = Vec::new();
    let mut got_local_symbols: Vec<String> = Vec::new();

    for (name, sym) in global_symbols.iter() {
        if sym.needs_plt {
            plt_symbols.push(name.clone());
        } else if sym.needs_got && !sym.needs_plt {
            if sym.is_dynamic {
                got_dyn_symbols.push(name.clone());
            } else {
                got_local_symbols.push(name.clone());
            }
        }
    }
    plt_symbols.sort();
    got_dyn_symbols.sort();
    got_local_symbols.sort();

    for (i, name) in plt_symbols.iter().enumerate() {
        if let Some(sym) = global_symbols.get_mut(name) {
            sym.plt_index = i;
            sym.got_index = i;
        }
    }
    // Dynamic GOT symbols come first (they need .dynsym entries + GLOB_DAT)
    for (i, name) in got_dyn_symbols.iter().enumerate() {
        if let Some(sym) = global_symbols.get_mut(name) {
            sym.got_index = plt_symbols.len() + i;
        }
    }
    // Local GOT symbols come after (filled at link time, no .dynsym needed)
    for (i, name) in got_local_symbols.iter().enumerate() {
        if let Some(sym) = global_symbols.get_mut(name) {
            sym.got_index = plt_symbols.len() + got_dyn_symbols.len() + i;
        }
    }

    let num_plt = plt_symbols.len();
    let num_got_total = plt_symbols.len() + got_dyn_symbols.len() + got_local_symbols.len();
    (plt_symbols, got_dyn_symbols, got_local_symbols, num_plt, num_got_total)
}

pub(super) fn collect_ifunc_symbols(
    global_symbols: &HashMap<String, LinkerSymbol>,
    is_static: bool,
) -> Vec<String> {
    if !is_static { return Vec::new(); }
    let mut ifunc_symbols: Vec<String> = global_symbols.iter()
        .filter(|(_, s)| s.is_defined && s.sym_type == STT_GNU_IFUNC)
        .map(|(n, _)| n.clone())
        .collect();
    ifunc_symbols.sort();
    ifunc_symbols
}

// ══════════════════════════════════════════════════════════════════════════════
// Linker script PROVIDE / ENTRY support
// ══════════════════════════════════════════════════════════════════════════════

/// Apply `PROVIDE(symbol = expr)` directives from a linker script.
///
/// PROVIDE creates a symbol definition **only** if the symbol is currently
/// undefined (or absent) in the global symbol table. If the symbol already
/// has a definition from an object file or library, the PROVIDE directive
/// is silently ignored — this matches GNU ld semantics.
///
/// `provide_directives` is a list of `(symbol_name, address)` pairs where
/// the address has already been evaluated from the linker script expression.
/// For address-relative expressions whose value depends on final layout
/// (e.g., `PROVIDE(__data_end = .)`), the caller should pass 0 here and
/// perform deferred resolution during the layout phase in `emit.rs`.
///
/// Each provided symbol is inserted as a globally-bound, defined symbol
/// with `output_section = usize::MAX` (address TBD at layout time).
pub(super) fn apply_provide_symbols(
    global_symbols: &mut HashMap<String, LinkerSymbol>,
    provide_directives: &[(String, u64)],
) {
    for (name, address) in provide_directives {
        // PROVIDE semantics: only define the symbol if it is NOT already
        // defined. An existing entry that is merely referenced (undefined)
        // counts as "not defined" and will be replaced.
        let already_defined = global_symbols
            .get(name)
            .map_or(false, |sym| sym.is_defined);

        if already_defined {
            continue;
        }

        // STT_NOTYPE (0) is the standard type for linker-generated symbols
        // that do not correspond to a function or data object in any input
        // file. This matches GNU ld behavior for PROVIDE symbols.
        const STT_NOTYPE: u8 = 0;

        let provided_sym = LinkerSymbol {
            address: *address as u32,
            size: 0,
            sym_type: STT_NOTYPE,
            binding: STB_GLOBAL,
            visibility: STV_DEFAULT,
            is_defined: true,
            needs_plt: false,
            needs_got: false,
            output_section: usize::MAX,
            section_offset: 0,
            plt_index: 0,
            got_index: 0,
            is_dynamic: false,
            dynlib: String::new(),
            needs_copy: false,
            copy_addr: 0,
            version: None,
            uses_textrel: false,
        };

        global_symbols.insert(name.clone(), provided_sym);
    }
}

/// Validate that the `ENTRY(symbol)` directive references a known symbol.
///
/// After symbol resolution, this function checks whether the entry point
/// symbol specified in a linker script exists in the global symbol table.
/// Following GNU ld behavior, a missing ENTRY symbol is **not** a hard
/// error — the linker falls back to `_start` or `main` in `emit.rs`.
/// Instead, a warning is emitted to stderr so the user is informed.
///
/// Returns `Ok(())` unconditionally. The warning is purely informational
/// and does not prevent linking from proceeding.
pub(super) fn validate_entry_symbol(
    global_symbols: &HashMap<String, LinkerSymbol>,
    entry_name: &str,
) -> Result<(), String> {
    if let Some(sym) = global_symbols.get(entry_name) {
        if !sym.is_defined {
            // Symbol exists but is not defined (only referenced) — warn
            eprintln!(
                "warning: ENTRY symbol '{}' is undefined; falling back to default entry point",
                entry_name
            );
        }
        // Symbol is present and defined — all good
    } else {
        // Symbol not found at all — warn and let emit.rs fall back
        eprintln!(
            "warning: ENTRY symbol '{}' not found; falling back to default entry point",
            entry_name
        );
    }
    Ok(())
}
