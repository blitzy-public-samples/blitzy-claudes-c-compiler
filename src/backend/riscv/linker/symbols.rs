//! Phase 3: Global symbol table construction.
//!
//! Builds the global symbol table from input objects, handles COMMON symbols,
//! marks PLT/GOT needs for dynamic linking, and identifies GOT entries for
//! PC-relative GOT references.

use std::collections::{HashMap, HashSet};
use super::elf_read::*;
use super::relocations::{
    GlobalSym, MergedSection,
    R_RISCV_GOT_HI20, R_RISCV_TLS_GOT_HI20, R_RISCV_TLS_GD_HI20,
    got_sym_key,
};

/// Build the global symbol table from all input objects.
///
/// For each non-local symbol, creates or updates a `GlobalSym` entry.
/// Handles SHN_UNDEF (undefined), SHN_ABS (absolute), SHN_COMMON (common/BSS),
/// and defined symbols. Weak-to-strong overrides are applied.
pub fn build_global_symbols(
    input_objs: &[(String, ElfObject)],
    sec_mapping: &HashMap<(usize, usize), (usize, u64)>,
    merged_sections: &mut Vec<MergedSection>,
    merged_map: &mut HashMap<String, usize>,
) -> HashMap<String, GlobalSym> {
    let mut global_syms: HashMap<String, GlobalSym> = HashMap::new();

    for (obj_idx, (_, obj)) in input_objs.iter().enumerate() {
        for sym in &obj.symbols {
            if sym.name.is_empty() || sym.binding() == STB_LOCAL {
                continue;
            }

            if sym.shndx == SHN_UNDEF {
                global_syms.entry(sym.name.clone()).or_insert_with(|| GlobalSym {
                    value: 0, size: 0, binding: sym.binding(),
                    sym_type: sym.sym_type(), visibility: sym.visibility(),
                    defined: false, needs_plt: false, plt_idx: 0,
                    got_offset: None, section_idx: None,
                });
                continue;
            }

            if sym.shndx == SHN_ABS {
                let entry = global_syms.entry(sym.name.clone()).or_insert_with(|| GlobalSym {
                    value: sym.value, size: sym.size, binding: sym.binding(),
                    sym_type: sym.sym_type(), visibility: sym.visibility(),
                    defined: true, needs_plt: false, plt_idx: 0,
                    got_offset: None, section_idx: None,
                });
                if !entry.defined || (entry.binding == STB_WEAK && sym.binding() == STB_GLOBAL) {
                    entry.value = sym.value;
                    entry.size = sym.size;
                    entry.binding = sym.binding();
                    entry.sym_type = sym.sym_type();
                    entry.defined = true;
                }
                continue;
            }

            if sym.shndx == SHN_COMMON {
                allocate_common_symbol(
                    sym, &mut global_syms, merged_sections, merged_map,
                );
                continue;
            }

            let sec_idx = sym.shndx as usize;
            let (merged_idx, offset) = match sec_mapping.get(&(obj_idx, sec_idx)) {
                Some(&v) => v,
                None => continue,
            };

            let entry = global_syms.entry(sym.name.clone()).or_insert_with(|| GlobalSym {
                value: 0, size: sym.size, binding: sym.binding(),
                sym_type: sym.sym_type(), visibility: sym.visibility(),
                defined: false, needs_plt: false, plt_idx: 0,
                got_offset: None, section_idx: None,
            });

            if entry.defined && !(entry.binding == STB_WEAK && sym.binding() == STB_GLOBAL) {
                continue;
            }

            entry.value = offset + sym.value;
            entry.size = sym.size;
            entry.binding = sym.binding();
            entry.sym_type = sym.sym_type();
            entry.visibility = sym.visibility();
            entry.defined = true;
            entry.section_idx = Some(merged_idx);
        }
    }

    global_syms
}

/// Allocate a COMMON symbol in .bss.
fn allocate_common_symbol(
    sym: &Symbol,
    global_syms: &mut HashMap<String, GlobalSym>,
    merged_sections: &mut Vec<MergedSection>,
    merged_map: &mut HashMap<String, usize>,
) {
    let bss_idx = *merged_map.entry(".bss".into()).or_insert_with(|| {
        let idx = merged_sections.len();
        merged_sections.push(MergedSection {
            name: ".bss".into(),
            sh_type: SHT_NOBITS,
            sh_flags: SHF_ALLOC | SHF_WRITE,
            data: Vec::new(),
            vaddr: 0,
            align: 8,
        });
        idx
    });
    let ms = &mut merged_sections[bss_idx];
    let align = sym.value.max(1) as usize; // st_value is alignment for COMMON
    let cur = ms.data.len();
    let aligned = (cur + align - 1) & !(align - 1);
    ms.data.resize(aligned, 0);
    let off = ms.data.len() as u64;
    ms.data.resize(ms.data.len() + sym.size as usize, 0);
    ms.align = ms.align.max(align as u64);

    let entry = global_syms.entry(sym.name.clone()).or_insert_with(|| GlobalSym {
        value: off, size: sym.size, binding: sym.binding(),
        sym_type: STT_OBJECT, visibility: sym.visibility(),
        defined: true, needs_plt: false, plt_idx: 0,
        got_offset: None, section_idx: Some(bss_idx),
    });
    if !entry.defined || (entry.binding == STB_WEAK && sym.binding() == STB_GLOBAL) {
        entry.value = off;
        entry.size = sym.size.max(entry.size);
        entry.binding = sym.binding();
        entry.defined = true;
        entry.section_idx = Some(bss_idx);
    }
}

/// Mark symbols that need PLT entries (undefined functions found in shared libs)
/// and collect symbols that need COPY relocations (undefined data objects).
///
/// Each copy symbol entry is (name, size, shlib_value) where shlib_value is the
/// symbol's address in the shared library. This enables alias coalescing: symbols
/// that point to the same shared-library address (e.g. `environ` and `__environ`
/// in glibc) will share a single BSS allocation, preserving the aliasing semantics
/// that the C library depends on.
///
/// Critically, this function also detects shared-library aliases: when a COPY symbol
/// (e.g. `environ`) has aliases in the shared library at the same address (e.g.
/// `__environ`, `_environ`), those aliases are also added as COPY symbols. This
/// ensures the dynamic linker redirects ALL aliases to the executable's BSS copy,
/// so writes through any alias name (e.g. glibc's `__libc_start_main` setting
/// `__environ = envp`) are visible through every alias.
pub fn mark_plt_and_copy_symbols(
    global_syms: &mut HashMap<String, GlobalSym>,
    shared_lib_syms: &HashMap<String, DynSymbol>,
) -> (Vec<String>, Vec<(String, u64, u64)>) {
    let mut plt_symbols: Vec<String> = Vec::new();
    let mut copy_symbols: Vec<(String, u64, u64)> = Vec::new();
    let mut copy_sym_set: HashSet<String> = HashSet::new();

    for (name, sym) in global_syms.iter_mut() {
        if !sym.defined {
            if let Some(shlib_sym) = shared_lib_syms.get(name) {
                if shlib_sym.sym_type() == STT_OBJECT {
                    copy_symbols.push((name.clone(), shlib_sym.size, shlib_sym.value));
                    copy_sym_set.insert(name.clone());
                } else {
                    sym.needs_plt = true;
                    sym.plt_idx = plt_symbols.len();
                    plt_symbols.push(name.clone());
                }
            }
        }
    }

    // Build a reverse map: shlib_value -> list of symbol names at that address.
    // This lets us find aliases (e.g. `__environ` for `environ`) that the executable
    // doesn't directly reference but must also COPY to maintain aliasing semantics.
    if !copy_symbols.is_empty() {
        let mut value_to_names: HashMap<u64, Vec<(&str, u64)>> = HashMap::new();
        for (name, dsym) in shared_lib_syms.iter() {
            if dsym.sym_type() == STT_OBJECT && dsym.value != 0 {
                value_to_names.entry(dsym.value).or_default().push((name.as_str(), dsym.size));
            }
        }

        // For each COPY symbol, find all aliases (other symbols at the same shlib address)
        // and add them as additional COPY entries with synthetic GlobalSym entries.
        let mut alias_additions: Vec<(String, u64, u64)> = Vec::new();
        for (name, _size, shlib_value) in &copy_symbols {
            if *shlib_value == 0 { continue; }
            if let Some(aliases) = value_to_names.get(shlib_value) {
                for &(alias_name, alias_size) in aliases {
                    if alias_name == name { continue; }
                    if copy_sym_set.contains(alias_name) { continue; }
                    // Add a synthetic GlobalSym for the alias so the linker emits it
                    // in the dynamic symbol table, enabling the dynamic linker to
                    // redirect the shared library's GOT entry for this alias.
                    if !global_syms.contains_key(alias_name) {
                        global_syms.insert(alias_name.to_string(), GlobalSym {
                            value: 0,
                            size: alias_size,
                            binding: STB_GLOBAL,
                            sym_type: STT_OBJECT,
                            visibility: 0,
                            defined: false,
                            needs_plt: false,
                            plt_idx: 0,
                            got_offset: None,
                            section_idx: None,
                        });
                    }
                    alias_additions.push((alias_name.to_string(), alias_size, *shlib_value));
                    copy_sym_set.insert(alias_name.to_string());
                }
            }
        }
        copy_symbols.extend(alias_additions);
    }

    (plt_symbols, copy_symbols)
}

/// Identify GOT entries needed by scanning for GOT_HI20 and TLS GOT relocations.
///
/// Returns the ordered list of GOT symbol keys, the set of TLS GOT symbols,
/// and a map of local GOT symbol info for resolving local GOT entries.
pub fn collect_got_entries(
    input_objs: &[(String, ElfObject)],
) -> (Vec<String>, HashSet<String>, HashMap<String, (usize, usize, i64)>) {
    let mut got_symbols: Vec<String> = Vec::new();
    let mut tls_got_symbols: HashSet<String> = HashSet::new();
    let mut local_got_sym_info: HashMap<String, (usize, usize, i64)> = HashMap::new();

    let mut got_set: HashSet<String> = HashSet::new();
    for (obj_idx, (_, obj)) in input_objs.iter().enumerate() {
        for relocs in &obj.relocations {
            for reloc in relocs {
                if reloc.rela_type == R_RISCV_GOT_HI20
                    || reloc.rela_type == R_RISCV_TLS_GOT_HI20
                    || reloc.rela_type == R_RISCV_TLS_GD_HI20
                {
                    let sym = &obj.symbols[reloc.sym_idx as usize];
                    let (name, is_local) = got_sym_key(obj_idx, sym, reloc.addend);
                    if !name.is_empty() && !got_set.contains(&name) {
                        got_set.insert(name.clone());
                        got_symbols.push(name.clone());
                        if is_local {
                            local_got_sym_info.insert(
                                name.clone(),
                                (obj_idx, reloc.sym_idx as usize, reloc.addend),
                            );
                        }
                    }
                    if reloc.rela_type == R_RISCV_TLS_GOT_HI20
                        || reloc.rela_type == R_RISCV_TLS_GD_HI20
                    {
                        tls_got_symbols.insert(name);
                    }
                }
            }
        }
    }

    (got_symbols, tls_got_symbols, local_got_sym_info)
}

/// Build local symbol virtual address table for relocation resolution.
pub fn build_local_sym_vaddrs(
    input_objs: &[(String, ElfObject)],
    sec_mapping: &HashMap<(usize, usize), (usize, u64)>,
    section_vaddrs: &[u64],
    global_syms: &HashMap<String, GlobalSym>,
) -> Vec<Vec<u64>> {
    let mut local_sym_vaddrs: Vec<Vec<u64>> = Vec::new();
    for (obj_idx, (_, obj)) in input_objs.iter().enumerate() {
        let mut sym_vaddrs = vec![0u64; obj.symbols.len()];
        for (si, sym) in obj.symbols.iter().enumerate() {
            if sym.shndx == SHN_UNDEF || sym.shndx == SHN_ABS {
                if sym.shndx == SHN_ABS {
                    sym_vaddrs[si] = sym.value;
                }
                continue;
            }
            if sym.shndx == SHN_COMMON {
                if let Some(gs) = global_syms.get(&sym.name) {
                    sym_vaddrs[si] = gs.value;
                }
                continue;
            }
            let sec_idx = sym.shndx as usize;
            if let Some(&(merged_idx, offset)) = sec_mapping.get(&(obj_idx, sec_idx)) {
                sym_vaddrs[si] = section_vaddrs[merged_idx] + offset + sym.value;
            }
        }
        local_sym_vaddrs.push(sym_vaddrs);
    }
    local_sym_vaddrs
}

/// Check for truly undefined symbols (not dynamic, not weak, not linker-defined).
pub fn check_undefined_symbols(
    global_syms: &HashMap<String, GlobalSym>,
    shared_lib_syms: &HashMap<String, DynSymbol>,
) -> Result<(), String> {
    let mut truly_undefined: Vec<&String> = global_syms.iter()
        .filter(|(name, sym)| {
            !sym.defined && !sym.needs_plt && sym.binding != STB_WEAK
                && !crate::backend::linker_common::is_linker_defined_symbol(name)
                && !shared_lib_syms.contains_key(name.as_str())
        })
        .map(|(name, _)| name)
        .collect();

    if !truly_undefined.is_empty() {
        truly_undefined.sort();
        truly_undefined.truncate(20);
        return Err(format!(
            "undefined symbols: {}",
            truly_undefined.iter().map(|s| s.as_str()).collect::<Vec<_>>().join(", ")
        ));
    }
    Ok(())
}
