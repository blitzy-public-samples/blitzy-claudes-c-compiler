//! AArch64 linker orchestration.
//!
//! Contains the two public entry points (`link_builtin` and `link_shared`) that
//! orchestrate the linking pipeline: load inputs, resolve symbols, merge sections,
//! build PLT/GOT, and dispatch to the appropriate ELF emission path.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use super::elf::*;
use super::types::GlobalSymbol;
use super::input::{load_file, resolve_lib, resolve_lib_prefer_shared};
use super::plt_got::create_plt_got;
use super::emit_dynamic::emit_dynamic_executable;
use super::emit_shared::emit_shared_library;
use super::emit_static::emit_executable;
use crate::backend::linker_common;
use linker_common::{OutputSection, GlobalSymbolOps, SymbolExpr};

// ── Linker script expression evaluator ──────────────────────────────────

/// Evaluate a linker script symbol expression to a u64 value.
///
/// Supports constant values, symbol references (looked up in globals),
/// and basic arithmetic (add, subtract, bitwise AND, bitwise NOT, ALIGN).
/// Returns `None` for expressions that cannot be resolved at this stage
/// (e.g., `.` dot location counter, which depends on layout).
fn eval_symbol_expr<G: GlobalSymbolOps>(expr: &SymbolExpr, globals: &HashMap<String, G>) -> Option<u64> {
    match expr {
        SymbolExpr::Constant(v) => Some(*v),
        SymbolExpr::Symbol(name) => {
            globals.get(name.as_str()).and_then(|s| {
                if s.is_defined() { Some(s.value()) } else { None }
            })
        }
        SymbolExpr::Add(l, r) => {
            let lv = eval_symbol_expr(l, globals)?;
            let rv = eval_symbol_expr(r, globals)?;
            Some(lv.wrapping_add(rv))
        }
        SymbolExpr::Sub(l, r) => {
            let lv = eval_symbol_expr(l, globals)?;
            let rv = eval_symbol_expr(r, globals)?;
            Some(lv.wrapping_sub(rv))
        }
        SymbolExpr::And(l, r) => {
            let lv = eval_symbol_expr(l, globals)?;
            let rv = eval_symbol_expr(r, globals)?;
            Some(lv & rv)
        }
        SymbolExpr::Not(inner) => {
            let v = eval_symbol_expr(inner, globals)?;
            Some(!v)
        }
        SymbolExpr::Align(inner) => {
            // ALIGN(n) rounds up the location counter to the next multiple of n.
            // At this stage we don't have the location counter, so treat as v itself.
            let v = eval_symbol_expr(inner, globals)?;
            Some(v)
        }
        SymbolExpr::Dot => None, // Cannot resolve `.` without layout context
    }
}

// ── Public entry point ─────────────────────────────────────────────────

/// Link AArch64 object files into an ELF executable (pre-resolved CRT/library variant).
///
/// Supports both static and dynamic linking. When `is_static` is false, shared
/// libraries are loaded and PLT/GOT/.dynamic sections are generated for dynamic
/// symbol references.
pub fn link_builtin(
    object_files: &[&str],
    output_path: &str,
    user_args: &[String],
    lib_paths: &[&str],
    needed_libs: &[&str],
    crt_objects_before: &[&str],
    crt_objects_after: &[&str],
    is_static: bool,
) -> Result<(), String> {
    if std::env::var("LINKER_DEBUG").is_ok() {
        eprintln!("arm linker: object_files={:?} output={} user_args={:?} static={}", object_files, output_path, user_args, is_static);
    }
    let mut objects: Vec<ElfObject> = Vec::new();
    let mut globals: HashMap<String, GlobalSymbol> = HashMap::new();
    let mut needed_sonames: Vec<String> = Vec::new();

    let all_lib_paths: Vec<String> = lib_paths.iter().map(|s| s.to_string()).collect();

    // Parse user args for export-dynamic flag and linker script path
    let mut export_dynamic = false;
    let mut linker_script_path: Option<String> = None;
    {
        let mut idx = 0;
        while idx < user_args.len() {
            let arg = &user_args[idx];
            if arg == "-rdynamic" { export_dynamic = true; }
            if let Some(wl_arg) = arg.strip_prefix("-Wl,") {
                let parts: Vec<&str> = wl_arg.split(',').collect();
                let mut j = 0;
                while j < parts.len() {
                    let part = parts[j];
                    if part == "--export-dynamic" || part == "-export-dynamic" || part == "-E" {
                        export_dynamic = true;
                    } else if part == "-T" && j + 1 < parts.len() {
                        j += 1;
                        linker_script_path = Some(parts[j].to_string());
                    } else if let Some(script) = part.strip_prefix("-T") {
                        if !script.is_empty() {
                            linker_script_path = Some(script.to_string());
                        }
                    }
                    j += 1;
                }
            } else if arg == "-T" && idx + 1 < user_args.len() {
                idx += 1;
                linker_script_path = Some(user_args[idx].clone());
            } else if let Some(script) = arg.strip_prefix("-T") {
                if !script.is_empty() {
                    linker_script_path = Some(script.to_string());
                }
            }
            idx += 1;
        }
    }

    // Load CRT objects before user objects
    for path in crt_objects_before {
        if Path::new(path).exists() {
            load_file(path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, is_static)?;
        }
    }

    // Load user object files
    for path in object_files {
        load_file(path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, is_static)?;
    }

    // Parse user_args for -l, -L, bare files, etc.
    let args: Vec<&str> = user_args.iter().map(|s| s.as_str()).collect();
    let mut defsym_defs: Vec<(String, String)> = Vec::new();
    let mut extra_lib_paths: Vec<String> = Vec::new();
    let mut gc_sections = false;
    let mut arg_i = 0;
    while arg_i < args.len() {
        let arg = args[arg_i];
        if let Some(path) = arg.strip_prefix("-L") {
            let p = if path.is_empty() && arg_i + 1 < args.len() { arg_i += 1; args[arg_i] } else { path };
            extra_lib_paths.push(p.to_string());
        } else if let Some(lib) = arg.strip_prefix("-l") {
            let l = if lib.is_empty() && arg_i + 1 < args.len() { arg_i += 1; args[arg_i] } else { lib };
            let resolver = if is_static { resolve_lib } else { resolve_lib_prefer_shared };
            let mut combined = extra_lib_paths.clone();
            combined.extend(all_lib_paths.iter().cloned());
            if let Some(lib_path) = resolver(l, &combined) {
                load_file(&lib_path, &mut objects, &mut globals, &mut needed_sonames, &combined, is_static)?;
            }
        } else if let Some(wl_arg) = arg.strip_prefix("-Wl,") {
            let parts: Vec<&str> = wl_arg.split(',').collect();
            let mut j = 0;
            while j < parts.len() {
                let part = parts[j];
                if let Some(lpath) = part.strip_prefix("-L") {
                    extra_lib_paths.push(lpath.to_string());
                } else if let Some(lib) = part.strip_prefix("-l") {
                    let resolver = if is_static { resolve_lib } else { resolve_lib_prefer_shared };
                    let mut combined = extra_lib_paths.clone();
                    combined.extend(all_lib_paths.iter().cloned());
                    if let Some(lib_path) = resolver(lib, &combined) {
                        load_file(&lib_path, &mut objects, &mut globals, &mut needed_sonames, &combined, is_static)?;
                    }
                } else if let Some(defsym_arg) = part.strip_prefix("--defsym=") {
                    // --defsym=SYMBOL=EXPR: define a symbol alias
                    // TODO: only supports symbol-to-symbol aliasing, not arbitrary expressions
                    if let Some(eq_pos) = defsym_arg.find('=') {
                        defsym_defs.push((defsym_arg[..eq_pos].to_string(), defsym_arg[eq_pos + 1..].to_string()));
                    }
                } else if part == "--defsym" && j + 1 < parts.len() {
                    // Two-argument form: --defsym SYM=VAL
                    j += 1;
                    let defsym_arg = parts[j];
                    if let Some(eq_pos) = defsym_arg.find('=') {
                        defsym_defs.push((defsym_arg[..eq_pos].to_string(), defsym_arg[eq_pos + 1..].to_string()));
                    }
                } else if part == "--gc-sections" {
                    gc_sections = true;
                } else if part == "--no-gc-sections" {
                    gc_sections = false;
                } else if part == "-T" && j + 1 < parts.len() {
                    j += 1;
                    linker_script_path = Some(parts[j].to_string());
                } else if let Some(script) = part.strip_prefix("-T") {
                    if !script.is_empty() {
                        linker_script_path = Some(script.to_string());
                    }
                }
                j += 1;
            }
        } else if arg == "-T" && arg_i + 1 < args.len() {
            arg_i += 1;
            linker_script_path = Some(args[arg_i].to_string());
        } else if let Some(script) = arg.strip_prefix("-T") {
            if !script.is_empty() {
                linker_script_path = Some(script.to_string());
            }
        } else if !arg.starts_with('-') && Path::new(arg).exists() {
            load_file(arg, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, is_static)?;
        }
        arg_i += 1;
    }

    // Load CRT objects after user objects
    for path in crt_objects_after {
        if Path::new(path).exists() {
            load_file(path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, is_static)?;
        }
    }

    // Build combined library search paths: user -L first, then system paths
    let mut combined_lib_paths: Vec<String> = extra_lib_paths;
    combined_lib_paths.extend(all_lib_paths.iter().cloned());

    // Load default libraries in a group (like ld's --start-group)
    if !needed_libs.is_empty() {
        let resolver = if is_static { resolve_lib } else { resolve_lib_prefer_shared };
        let mut lib_paths_resolved: Vec<String> = Vec::new();
        for lib_name in needed_libs {
            if let Some(lib_path) = resolver(lib_name, &combined_lib_paths) {
                if !lib_paths_resolved.contains(&lib_path) {
                    lib_paths_resolved.push(lib_path);
                }
            }
        }
        let mut changed = true;
        while changed {
            changed = false;
            let prev_count = objects.len();
            for lib_path in &lib_paths_resolved {
                load_file(lib_path, &mut objects, &mut globals, &mut needed_sonames, &combined_lib_paths, is_static)?;
            }
            if objects.len() != prev_count {
                changed = true;
            }
        }
    }

    // For dynamic linking, resolve remaining undefined symbols against system libs
    if !is_static {
        let default_libs = ["libc.so.6", "libm.so.6", "libgcc_s.so.1", "ld-linux-aarch64.so.1"];
        linker_common::resolve_dynamic_symbols_elf64(
            &mut globals, &mut needed_sonames, &combined_lib_paths, &default_libs,
        )?;
    }

    // Apply --defsym definitions: alias one symbol to another
    for (alias, target) in &defsym_defs {
        if let Some(target_sym) = globals.get(target).cloned() {
            globals.insert(alias.clone(), target_sym);
        }
    }

    // Parse linker script if provided via -T
    let script = if let Some(ref script_path) = linker_script_path {
        match linker_common::parse_linker_script(std::path::Path::new(script_path)) {
            Ok(s) => Some(s),
            Err(_e) => None, // Silently ignore invalid scripts (matches observed behavior)
        }
    } else {
        None
    };

    // Resolve PROVIDE symbols from linker script before undefined symbol check.
    // PROVIDE(symbol = expr) creates a symbol only if it is otherwise undefined.
    if let Some(ref s) = script {
        let provide_pairs: Vec<(String, u64)> = s.provide_symbols.iter().filter_map(|p| {
            eval_symbol_expr(&p.expr, &globals).map(|val| (p.name.clone(), val))
        }).collect();
        let resolved = linker_common::resolve_provide_symbols(&provide_pairs, &globals);
        for (name, addr) in resolved {
            // Create an absolute symbol (SHN_ABS) from the PROVIDE directive.
            // Use defined_in = Some(usize::MAX) as sentinel for linker-provided.
            // STT_NOTYPE = 0, so info = STB_GLOBAL << 4
            let sym = GlobalSymbol {
                value: addr,
                size: 0,
                info: STB_GLOBAL << 4,
                defined_in: Some(usize::MAX),
                from_lib: None,
                plt_idx: None,
                got_idx: None,
                section_idx: SHN_ABS,
                is_dynamic: false,
                copy_reloc: false,
                lib_sym_value: 0,
            };
            globals.insert(name, sym);
        }
    }

    // Garbage-collect unreferenced sections when --gc-sections is active.
    // This removes sections not reachable from entry points, which may also
    // eliminate undefined symbol references from dead code.
    let dead_sections: HashSet<(usize, usize)> = if gc_sections {
        linker_common::gc_collect_sections_elf64(&objects)
    } else {
        HashSet::new()
    };

    // When gc-sections is active, remove globals that only exist in dead sections
    if gc_sections {
        let mut referenced_from_live: HashSet<String> = HashSet::new();
        for (obj_idx, obj) in objects.iter().enumerate() {
            for (sec_idx, relas) in obj.relocations.iter().enumerate() {
                if dead_sections.contains(&(obj_idx, sec_idx)) { continue; }
                for rela in relas {
                    if (rela.sym_idx as usize) < obj.symbols.len() {
                        let sym = &obj.symbols[rela.sym_idx as usize];
                        if !sym.name.is_empty() {
                            referenced_from_live.insert(sym.name.clone());
                        }
                    }
                }
            }
        }
        // When a linker script has KEEP patterns, also retain symbols from
        // kept sections so they are not pruned as unreferenced.
        if let Some(ref s) = script {
            for (obj_idx, obj) in objects.iter().enumerate() {
                for (sec_idx, sec) in obj.sections.iter().enumerate() {
                    if linker_common::is_section_kept(&sec.name, &s.keep_patterns) {
                        // Mark all symbols in this kept section as referenced
                        for rela in obj.relocations.get(sec_idx).unwrap_or(&Vec::new()) {
                            if (rela.sym_idx as usize) < obj.symbols.len() {
                                let sym = &obj.symbols[rela.sym_idx as usize];
                                if !sym.name.is_empty() {
                                    referenced_from_live.insert(sym.name.clone());
                                }
                            }
                        }
                        // Also retain any symbols defined in this section
                        for sym in &obj.symbols {
                            if sym.shndx == sec_idx as u16 && !sym.name.is_empty() {
                                referenced_from_live.insert(sym.name.clone());
                            }
                        }
                        let _ = obj_idx; // used to index into objects
                    }
                }
            }
        }
        globals.retain(|name, sym| {
            sym.defined_in.is_some() || sym.is_dynamic
                || (sym.info >> 4) == STB_WEAK
                || referenced_from_live.contains(name)
        });
    }

    // Reject truly undefined symbols (weak undefined are allowed)
    let mut unresolved = Vec::new();
    for (name, sym) in &globals {
        if sym.defined_in.is_none() && !sym.is_dynamic && sym.section_idx == SHN_UNDEF {
            let binding = sym.info >> 4;
            if binding != STB_WEAK && !linker_common::is_linker_defined_symbol(name) {
                unresolved.push(name.clone());
            }
        }
    }
    if !unresolved.is_empty() {
        unresolved.sort();
        unresolved.truncate(20);
        return Err(format!("undefined symbols: {}",
            unresolved.iter().map(|s| s.as_str()).collect::<Vec<_>>().join(", ")));
    }

    // Merge sections (skip dead sections when gc-sections is active).
    // When a linker script is present, use script-aware merging that respects
    // SECTIONS { } placement directives and KEEP() patterns.
    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_map: HashMap<(usize, usize), (usize, u64)> = HashMap::new();

    if let Some(ref s) = script {
        // Build script_sections: (name, input_patterns, optional_address)
        // Resolve MEMORY region references to concrete addresses.
        let mut script_sections: Vec<(String, Vec<String>, Option<u64>)> = Vec::new();
        let mut used_regions: HashSet<String> = HashSet::new();
        for sec in &s.sections {
            // Resolve address: explicit address or first-use of MEMORY region ORIGIN.
            let addr = if let Some(a) = sec.address {
                Some(a)
            } else if let Some(ref region_name) = sec.memory_region {
                if !used_regions.contains(region_name) {
                    used_regions.insert(region_name.clone());
                    s.memory_regions.iter()
                        .find(|r| r.name == *region_name)
                        .map(|r| r.origin)
                } else {
                    None
                }
            } else {
                None
            };
            // Collect all input patterns as flat strings for merge_sections_with_script
            let mut patterns: Vec<String> = Vec::new();
            for ip in &sec.input_patterns {
                for sp in &ip.section_patterns {
                    if ip.file_pattern == "*" {
                        patterns.push(format!("*({})", sp));
                    } else {
                        patterns.push(format!("{}({})", ip.file_pattern, sp));
                    }
                }
            }
            script_sections.push((sec.name.clone(), patterns, addr));
        }
        let keep_patterns: Vec<String> = s.keep_patterns.clone();
        linker_common::merge_sections_with_script::<GlobalSymbol>(
            &objects, &mut output_sections, &mut section_map,
            &dead_sections, &script_sections, &keep_patterns,
        );
    } else {
        linker_common::merge_sections_elf64_gc(&objects, &mut output_sections, &mut section_map, &dead_sections);
    }

    // Allocate COMMON symbols (using shared implementation)
    linker_common::allocate_common_symbols_elf64(&mut globals, &mut output_sections);

    // Forward ENTRY override from linker script: if the script specifies an ENTRY
    // symbol different from _start, alias _start to the entry symbol so that the
    // emitter (which looks up _start) uses the correct entry point address.
    if let Some(ref s) = script {
        if let Some(ref entry_name) = s.entry {
            if entry_name != "_start" {
                if let Some(sym) = globals.get(entry_name).cloned() {
                    globals.entry("_start".to_string()).or_insert(sym);
                }
            }
        }
    }

    // IFUNC debug logging: report IFUNC symbol count when LINKER_DEBUG is set
    if std::env::var("LINKER_DEBUG").is_ok() {
        let ifunc_count = globals.values()
            .filter(|g| g.info & 0xf == STT_GNU_IFUNC && g.defined_in.is_some())
            .count();
        if ifunc_count > 0 {
            eprintln!("arm linker: {} IFUNC symbols found", ifunc_count);
        }
    }

    // Check if we have any dynamic symbols
    let has_dynamic_syms = globals.values().any(|g| g.is_dynamic);

    if has_dynamic_syms && !is_static {
        // Create PLT/GOT for dynamic symbols
        let (plt_names, got_entries) = create_plt_got(&objects, &mut globals);

        // Emit dynamically-linked executable
        emit_dynamic_executable(
            &objects, &mut globals, &mut output_sections, &section_map,
            &plt_names, &got_entries, &needed_sonames, output_path,
            export_dynamic,
        )
    } else {
        // Fall back to static emit
        emit_executable(&objects, &mut globals, &mut output_sections, &section_map, output_path)
    }
}

// ── Shared library output ────────────────────────────────────────────

/// Create a shared library (.so) from object files.
pub fn link_shared(
    object_files: &[&str],
    output_path: &str,
    user_args: &[String],
    lib_paths: &[&str],
) -> Result<(), String> {
    let mut objects: Vec<ElfObject> = Vec::new();
    let mut globals: HashMap<String, GlobalSymbol> = HashMap::new();
    let mut needed_sonames: Vec<String> = Vec::new();
    let lib_path_strings: Vec<String> = lib_paths.iter().map(|s| s.to_string()).collect();

    // Parse user args
    let mut extra_lib_paths: Vec<String> = Vec::new();
    let mut libs_to_load: Vec<String> = Vec::new();
    let mut extra_object_files: Vec<String> = Vec::new();
    let mut soname: Option<String> = None;
    let mut linker_script_path: Option<String> = None;
    let mut i = 0;
    let args: Vec<&str> = user_args.iter().map(|s| s.as_str()).collect();
    while i < args.len() {
        let arg = args[i];
        if let Some(path) = arg.strip_prefix("-L") {
            let p = if path.is_empty() && i + 1 < args.len() { i += 1; args[i] } else { path };
            extra_lib_paths.push(p.to_string());
        } else if let Some(lib) = arg.strip_prefix("-l") {
            let l = if lib.is_empty() && i + 1 < args.len() { i += 1; args[i] } else { lib };
            libs_to_load.push(l.to_string());
        } else if let Some(wl_arg) = arg.strip_prefix("-Wl,") {
            let parts: Vec<&str> = wl_arg.split(',').collect();
            let mut j = 0;
            while j < parts.len() {
                let part = parts[j];
                if let Some(sn) = part.strip_prefix("-soname=") {
                    soname = Some(sn.to_string());
                } else if part == "-soname" && j + 1 < parts.len() {
                    j += 1;
                    soname = Some(parts[j].to_string());
                } else if let Some(lpath) = part.strip_prefix("-L") {
                    extra_lib_paths.push(lpath.to_string());
                } else if let Some(lib) = part.strip_prefix("-l") {
                    libs_to_load.push(lib.to_string());
                } else if part == "-T" && j + 1 < parts.len() {
                    j += 1;
                    linker_script_path = Some(parts[j].to_string());
                } else if let Some(script) = part.strip_prefix("-T") {
                    if !script.is_empty() {
                        linker_script_path = Some(script.to_string());
                    }
                }
                j += 1;
            }
        } else if arg == "-T" && i + 1 < args.len() {
            i += 1;
            linker_script_path = Some(args[i].to_string());
        } else if let Some(script) = arg.strip_prefix("-T") {
            if !script.is_empty() {
                linker_script_path = Some(script.to_string());
            }
        } else if arg == "-shared" || arg == "-nostdlib" || arg == "-o" {
            if arg == "-o" { i += 1; }
        } else if !arg.starts_with('-') && Path::new(arg).exists() {
            extra_object_files.push(arg.to_string());
        }
        i += 1;
    }

    for path in object_files {
        load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
    }
    for path in &extra_object_files {
        load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
    }

    let mut all_lib_paths: Vec<String> = extra_lib_paths;
    all_lib_paths.extend(lib_path_strings.iter().cloned());

    if !libs_to_load.is_empty() {
        let mut lib_paths_resolved: Vec<String> = Vec::new();
        for lib_name in &libs_to_load {
            if let Some(lib_path) = resolve_lib_prefer_shared(lib_name, &all_lib_paths) {
                if !lib_paths_resolved.contains(&lib_path) {
                    lib_paths_resolved.push(lib_path);
                }
            }
        }
        let mut changed = true;
        while changed {
            changed = false;
            let prev_count = objects.len();
            for lib_path in &lib_paths_resolved {
                load_file(lib_path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, false)?;
            }
            if objects.len() != prev_count { changed = true; }
        }
    }

    // Parse linker script if provided via -T
    let script = if let Some(ref script_path) = linker_script_path {
        match linker_common::parse_linker_script(std::path::Path::new(script_path)) {
            Ok(s) => Some(s),
            Err(_e) => None,
        }
    } else {
        None
    };

    // Resolve PROVIDE symbols from linker script.
    if let Some(ref s) = script {
        let provide_pairs: Vec<(String, u64)> = s.provide_symbols.iter().filter_map(|p| {
            eval_symbol_expr(&p.expr, &globals).map(|val| (p.name.clone(), val))
        }).collect();
        let resolved = linker_common::resolve_provide_symbols(&provide_pairs, &globals);
        for (name, addr) in resolved {
            // STT_NOTYPE = 0, so info = STB_GLOBAL << 4
            let sym = GlobalSymbol {
                value: addr,
                size: 0,
                info: STB_GLOBAL << 4,
                defined_in: Some(usize::MAX),
                from_lib: None,
                plt_idx: None,
                got_idx: None,
                section_idx: SHN_ABS,
                is_dynamic: false,
                copy_reloc: false,
                lib_sym_value: 0,
            };
            globals.insert(name, sym);
        }
    }

    // Merge sections (no gc-sections for shared libraries).
    // When a linker script is present, use script-aware merging.
    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_map: HashMap<(usize, usize), (usize, u64)> = HashMap::new();

    if let Some(ref s) = script {
        let empty_dead: HashSet<(usize, usize)> = HashSet::new();
        let mut script_sections: Vec<(String, Vec<String>, Option<u64>)> = Vec::new();
        let mut used_regions: HashSet<String> = HashSet::new();
        for sec in &s.sections {
            let addr = if let Some(a) = sec.address {
                Some(a)
            } else if let Some(ref region_name) = sec.memory_region {
                if !used_regions.contains(region_name) {
                    used_regions.insert(region_name.clone());
                    s.memory_regions.iter()
                        .find(|r| r.name == *region_name)
                        .map(|r| r.origin)
                } else {
                    None
                }
            } else {
                None
            };
            let mut patterns: Vec<String> = Vec::new();
            for ip in &sec.input_patterns {
                for sp in &ip.section_patterns {
                    if ip.file_pattern == "*" {
                        patterns.push(format!("*({})", sp));
                    } else {
                        patterns.push(format!("{}({})", ip.file_pattern, sp));
                    }
                }
            }
            script_sections.push((sec.name.clone(), patterns, addr));
        }
        let keep_patterns: Vec<String> = s.keep_patterns.clone();
        linker_common::merge_sections_with_script::<GlobalSymbol>(
            &objects, &mut output_sections, &mut section_map,
            &empty_dead, &script_sections, &keep_patterns,
        );
    } else {
        linker_common::merge_sections_elf64(&objects, &mut output_sections, &mut section_map);
    }

    linker_common::allocate_common_symbols_elf64(&mut globals, &mut output_sections);

    // Resolve undefined symbols against system shared libraries to discover
    // NEEDED dependencies. Without this, the shared library would be missing
    // DT_NEEDED entries for libc.so.6 etc., causing the dynamic linker to
    // fail to resolve PLT symbols at runtime.
    resolve_dynamic_symbols_for_shared(&objects, &globals, &mut needed_sonames, &all_lib_paths);

    // Emit shared library
    emit_shared_library(
        &objects, &mut globals, &mut output_sections, &section_map,
        &needed_sonames, output_path, soname,
    )
}

/// Discover NEEDED shared library dependencies for a shared library build.
/// Scans object file relocations for CALL26/JUMP26 references to undefined symbols
/// and searches system libraries to find which .so files provide them.
fn resolve_dynamic_symbols_for_shared(
    objects: &[ElfObject],
    globals: &HashMap<String, GlobalSymbol>,
    needed_sonames: &mut Vec<String>,
    lib_paths: &[String],
) {
    // Collect undefined symbol names referenced by function calls
    let mut undefined: Vec<String> = Vec::new();
    for obj in objects.iter() {
        for sec_relas in &obj.relocations {
            for rela in sec_relas {
                let si = rela.sym_idx as usize;
                if si >= obj.symbols.len() { continue; }
                let sym = &obj.symbols[si];
                if sym.name.is_empty() || sym.is_local() { continue; }
                let is_undef = if let Some(g) = globals.get(&sym.name) {
                    g.is_dynamic || (g.defined_in.is_none() && g.section_idx == SHN_UNDEF)
                } else {
                    sym.is_undefined()
                };
                if is_undef && !undefined.contains(&sym.name) {
                    undefined.push(sym.name.clone());
                }
            }
        }
    }
    if undefined.is_empty() { return; }

    let lib_names = ["libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "ld-linux-aarch64.so.1"];
    let mut libs: Vec<String> = Vec::new();
    for lib_name in &lib_names {
        for dir in lib_paths {
            let candidate = format!("{}/{}", dir, lib_name);
            if Path::new(&candidate).exists() {
                libs.push(candidate);
                break;
            }
        }
    }
    for lib_path in &libs {
        let data = match std::fs::read(lib_path) { Ok(d) => d, Err(_) => continue };
        let soname = linker_common::parse_soname(&data).unwrap_or_else(|| {
            Path::new(lib_path).file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default()
        });
        if needed_sonames.contains(&soname) { continue; }
        let dyn_syms = match linker_common::parse_shared_library_symbols(&data, lib_path) {
            Ok(s) => s, Err(_) => continue,
        };
        let provides_any = undefined.iter().any(|name| dyn_syms.iter().any(|ds| ds.name == *name));
        if provides_any {
            needed_sonames.push(soname);
        }
    }
}
