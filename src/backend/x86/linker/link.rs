//! x86-64 linker orchestration.
//!
//! Contains the two public entry points (`link_builtin` and `link_shared`) that
//! orchestrate the linking pipeline: load inputs, resolve symbols, merge sections,
//! build PLT/GOT, and dispatch to the appropriate ELF emission path.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use super::elf::*;
use super::types::GlobalSymbol;
use super::input::load_file;
use super::plt_got::{collect_ifunc_symbols, create_plt_got};
use super::emit_exec::emit_executable;
use super::emit_shared::emit_shared_library;
use crate::backend::linker_common::{
    self, OutputSection, GlobalSymbolOps, SymbolExpr,
    parse_linker_script, LinkerScript,
};

/// Evaluate a linker script symbol expression to a u64 value.
///
/// Supports constant values, symbol references (looked up in globals),
/// and basic arithmetic (add, subtract). Returns `None` for expressions
/// that cannot be resolved at this stage (e.g., `.` dot location counter,
/// which depends on layout that hasn't happened yet).
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
            let v = eval_symbol_expr(inner, globals)?;
            // ALIGN(n) rounds up the location counter to the next multiple of n
            // At this stage we don't have the location counter, so treat as v itself
            Some(v)
        }
        SymbolExpr::Dot => None, // Cannot resolve `.` without layout context
    }
}

pub fn link_builtin(
    object_files: &[&str],
    output_path: &str,
    user_args: &[String],
    lib_paths: &[&str],
    needed_libs: &[&str],
    crt_objects_before: &[&str],
    crt_objects_after: &[&str],
) -> Result<(), String> {
    let is_static = user_args.iter().any(|a| a == "-static");
    let mut objects: Vec<ElfObject> = Vec::new();
    let mut globals: HashMap<String, GlobalSymbol> = HashMap::new();
    let mut needed_sonames: Vec<String> = Vec::new();
    let lib_path_strings: Vec<String> = lib_paths.iter().map(|s| s.to_string()).collect();

    // Load CRT objects before user objects
    for path in crt_objects_before {
        if Path::new(path).exists() {
            load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
        }
    }

    // Load user object files
    for path in object_files {
        load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
    }

    // Parse user args using shared infrastructure
    let parsed_args = linker_common::parse_linker_args(user_args);
    let extra_lib_paths = parsed_args.extra_lib_paths;
    let libs_to_load = parsed_args.libs_to_load;
    let extra_object_files = parsed_args.extra_object_files;
    let export_dynamic = parsed_args.export_dynamic;
    let rpath_entries = parsed_args.rpath_entries;
    let use_runpath = parsed_args.use_runpath;
    let defsym_defs = parsed_args.defsym_defs;
    let gc_sections = parsed_args.gc_sections;
    let linker_script_path = parsed_args.linker_script;

    // Load extra .o files immediately; archives (.a) and shared libraries (.so)
    // are deferred to the group resolution loop. Archives need iterative re-scanning
    // for circular dependencies, and shared libraries must be processed after
    // archive members are extracted so they can resolve symbols introduced by
    // those members (e.g., QEMU's libqemuutil.a members reference libglib-2.0.so).
    let mut deferred_libs: Vec<String> = Vec::new();
    for path in &extra_object_files {
        if path.ends_with(".a") || path.ends_with(".so") || path.contains(".so.") {
            deferred_libs.push(path.clone());
        } else {
            load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
        }
    }

    let mut all_lib_paths: Vec<String> = extra_lib_paths;
    all_lib_paths.extend(lib_path_strings.iter().cloned());

    // Load CRT objects after
    for path in crt_objects_after {
        if Path::new(path).exists() {
            load_file(path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, false)?;
        }
    }

    // Load needed libraries using group resolution (like ld's --start-group/--end-group).
    // This iterates all archives and shared libraries until no new objects are pulled
    // in, handling circular dependencies between archives and ensuring shared libraries
    // can resolve symbols introduced by archive member extraction.
    {
        let mut all_lib_names: Vec<String> = needed_libs.iter().map(|s| s.to_string()).collect();
        all_lib_names.extend(libs_to_load.iter().cloned());

        let mut lib_paths_resolved: Vec<String> = Vec::new();
        // Include deferred .a and .so files first (preserving command-line order)
        for lib_path in &deferred_libs {
            if !lib_paths_resolved.contains(lib_path) {
                lib_paths_resolved.push(lib_path.clone());
            }
        }
        let needed_lib_count = needed_libs.len();
        for (idx, lib_name) in all_lib_names.iter().enumerate() {
            if let Some(lib_path) = linker_common::resolve_lib(lib_name, &all_lib_paths, is_static) {
                if !lib_paths_resolved.contains(&lib_path) {
                    lib_paths_resolved.push(lib_path);
                }
            } else if idx >= needed_lib_count {
                // User-specified -l library not found: error (matching ld behavior)
                return Err(format!("cannot find -l{}: No such file or directory", lib_name));
            }
        }

        // Group loading: iterate until stable. Track both object count changes
        // (from archive member extraction) and dynamic symbol count changes
        // (from shared library resolution) since either can introduce work for
        // the other on the next iteration.
        let mut changed = true;
        while changed {
            changed = false;
            let prev_obj_count = objects.len();
            let prev_dyn_count = needed_sonames.len();
            for lib_path in &lib_paths_resolved {
                load_file(lib_path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, false)?;
            }
            if objects.len() != prev_obj_count || needed_sonames.len() != prev_dyn_count {
                changed = true;
            }
        }
    }

    // Resolve remaining undefined symbols from default system libraries
    // (only when dynamically linking)
    if !is_static {
        let default_libs = ["libc.so.6", "libm.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2"];
        linker_common::resolve_dynamic_symbols_elf64(
            &mut globals, &mut needed_sonames, &all_lib_paths, &default_libs,
        )?;
    }

    // Apply --defsym definitions: alias one symbol to another
    for (alias, target) in &defsym_defs {
        if let Some(target_sym) = globals.get(target).cloned() {
            globals.insert(alias.clone(), target_sym);
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
    // and also remove references from dead sections
    if gc_sections {
        // Build set of symbols referenced only from dead sections
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
        // Remove undefined globals that are only referenced from dead sections
        globals.retain(|name, sym| {
            // Keep defined symbols, dynamic symbols, weak symbols, and those referenced from live code
            sym.defined_in.is_some() || sym.is_dynamic
                || (sym.info >> 4) == STB_WEAK
                || referenced_from_live.contains(name)
        });
    }

    // Parse linker script if provided via -T.
    // The linker script controls section placement, memory layout, entry point,
    // and conditional symbol generation (PROVIDE). Used primarily by kernel and
    // embedded builds where precise memory layout is required.
    let script: Option<LinkerScript> = if let Some(ref script_path) = linker_script_path {
        Some(parse_linker_script(Path::new(script_path))?)
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
            let sym = GlobalSymbol {
                value: addr,
                size: 0,
                info: (STB_GLOBAL << 4) | STT_NOTYPE,
                defined_in: Some(usize::MAX),
                from_lib: None,
                plt_idx: None,
                got_idx: None,
                section_idx: SHN_ABS,
                is_dynamic: false,
                copy_reloc: false,
                lib_sym_value: 0,
                version: None,
            };
            globals.insert(name, sym);
        }
    }

    // Apply ENTRY directive from linker script.
    // When the linker script specifies ENTRY(symbol), that symbol's address
    // becomes the ELF e_entry field. For the common case of ENTRY(_start),
    // this is already handled by emit_executable. For non-_start entry symbols
    // (e.g., kernel builds with a custom entry), ensure _start is set up as
    // an alias so that the executable emitter picks up the correct address.
    if let Some(ref s) = script {
        if let Some(ref entry_name) = s.entry {
            if entry_name != "_start" {
                if let Some(entry_sym) = globals.get(entry_name).cloned() {
                    if entry_sym.defined_in.is_some() {
                        // If _start is not already defined by an object file,
                        // create it pointing to the ENTRY symbol's address.
                        let start_defined = globals
                            .get("_start")
                            .map_or(false, |s| s.defined_in.is_some());
                        if !start_defined {
                            globals.insert("_start".to_string(), entry_sym);
                        }
                    }
                }
            }
        }
    }

    // Emit NSS warning for static linking.
    // When linking statically, glibc's NSS (Name Service Switch) functions
    // (getaddrinfo, getpwnam, gethostbyname, etc.) require shared libraries
    // at runtime even though the binary is statically linked. This is a common
    // source of subtle runtime failures, so we emit a diagnostic.
    linker_common::check_nss_static_warning(&globals, is_static);

    // Check for truly undefined (non-weak, non-dynamic, non-linker-defined) symbols
    linker_common::check_undefined_symbols_elf64(&globals, 20)?;

    // Merge sections (skip dead sections when gc-sections is active).
    // When a linker script is present, use script-aware merging that respects
    // SECTIONS { } placement directives and KEEP() patterns.
    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_map: HashMap<(usize, usize), (usize, u64)> = HashMap::new();

    if let Some(ref s) = script {
        // Build script_sections: (name, input_patterns, optional_address)
        // Resolve MEMORY region references to concrete addresses.
        let mut script_sections: Vec<(String, Vec<String>, Option<u64>)> = Vec::new();
        // Track which memory regions have been assigned to avoid double-assigning
        // the same origin to multiple sections.
        let mut used_regions: HashSet<String> = HashSet::new();
        for sec in &s.sections {
            // Resolve address: only explicit `. = addr;` and first-use of MEMORY
            // region ORIGIN get concrete addresses. Sections without explicit
            // addresses are placed sequentially by the layout engine in emit_exec.
            let addr = if let Some(a) = sec.address {
                Some(a)
            } else if let Some(ref region_name) = sec.memory_region {
                // Use the memory region ORIGIN for the first section placed in
                // that region. Subsequent sections in the same region get None
                // so the layout engine places them sequentially after the first.
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

    // Allocate COMMON symbols
    linker_common::allocate_common_symbols_elf64(&mut globals, &mut output_sections);

    // Create PLT/GOT
    let (plt_names, got_entries) = create_plt_got(&objects, &mut globals);

    // Collect GNU IFUNC (indirect function) symbols for dispatch.
    //
    // x86-64 fully supports GNU IFUNC in both static and dynamic linking modes:
    //
    //   Static linking: IFUNC resolver functions are called at program startup
    //   via R_X86_64_IRELATIVE relocations in the .rela.iplt section. The CRT
    //   iterates __rela_iplt_start..__rela_iplt_end and invokes each resolver,
    //   storing the result in the corresponding .iplt GOT slot. Subsequent
    //   calls go through the .iplt PLT stub which loads from the resolved GOT.
    //
    //   Dynamic linking: IFUNC symbols are handled through the standard PLT/GOT
    //   infrastructure. The dynamic linker (ld-linux-x86-64.so.2) resolves
    //   STT_GNU_IFUNC symbols by calling the resolver function during lazy or
    //   eager binding, then patching the GOT entry with the result.
    //
    // Both paths are handled by collect_ifunc_symbols (which gathers
    // STT_GNU_IFUNC globals) and the corresponding emission code in
    // emit_exec.rs (static: .iplt/.rela.iplt) and plt_got.rs (dynamic: PLT/GOT).
    let ifunc_symbols = collect_ifunc_symbols(&globals, is_static);

    // Emit executable
    emit_executable(
        &objects, &mut globals, &mut output_sections, &section_map,
        &plt_names, &got_entries, &needed_sonames, output_path,
        export_dynamic, &rpath_entries, use_runpath, is_static,
        &ifunc_symbols,
    )
}

/// Create a shared library (.so) from object files.
///
/// Produces an ELF `ET_DYN` file with position-independent base address (0),
/// exporting all defined global symbols. Used when the compiler is invoked
/// with `-shared`.
pub fn link_shared(
    object_files: &[&str],
    output_path: &str,
    user_args: &[String],
    lib_paths: &[&str],
    needed_libs: &[&str],
) -> Result<(), String> {
    let mut objects: Vec<ElfObject> = Vec::new();
    let mut globals: HashMap<String, GlobalSymbol> = HashMap::new();
    let mut needed_sonames: Vec<String> = Vec::new();
    let lib_path_strings: Vec<String> = lib_paths.iter().map(|s| s.to_string()).collect();

    // Parse user args for -L, -l, -Wl,-soname=, bare .o/.a files.
    // --whole-archive / --no-whole-archive are positional flags that affect
    // archives appearing after them, so we collect ordered items with their state.
    let mut extra_lib_paths: Vec<String> = Vec::new();
    let mut soname: Option<String> = None;
    let mut rpath_entries: Vec<String> = Vec::new();
    let mut use_runpath = false; // --enable-new-dtags -> DT_RUNPATH instead of DT_RPATH
    let mut pending_rpath = false; // for -Wl,-rpath -Wl,/path two-arg form
    let mut pending_soname = false; // for -Wl,-soname -Wl,name two-arg form
    let mut whole_archive = false;

    // Ordered list of items to load: (path_or_lib, is_lib, whole_archive_state)
    // is_lib=true means resolve via -l; is_lib=false means bare file path
    let mut ordered_items: Vec<(String, bool, bool)> = Vec::new();

    let mut i = 0;
    let args: Vec<&str> = user_args.iter().map(|s| s.as_str()).collect();
    while i < args.len() {
        let arg = args[i];
        if let Some(path) = arg.strip_prefix("-L") {
            let p = if path.is_empty() && i + 1 < args.len() { i += 1; args[i] } else { path };
            extra_lib_paths.push(p.to_string());
        } else if let Some(lib) = arg.strip_prefix("-l") {
            let l = if lib.is_empty() && i + 1 < args.len() { i += 1; args[i] } else { lib };
            ordered_items.push((l.to_string(), true, whole_archive));
        } else if let Some(wl_arg) = arg.strip_prefix("-Wl,") {
            let parts: Vec<&str> = wl_arg.split(',').collect();
            // Handle -Wl,-rpath -Wl,/path and -Wl,-soname -Wl,name two-arg forms
            if (pending_rpath || pending_soname) && !parts.is_empty() {
                if pending_rpath {
                    rpath_entries.push(parts[0].to_string());
                    pending_rpath = false;
                } else if pending_soname {
                    soname = Some(parts[0].to_string());
                    pending_soname = false;
                }
                i += 1;
                continue;
            }
            let mut j = 0;
            while j < parts.len() {
                let part = parts[j];
                if let Some(sn) = part.strip_prefix("-soname=") {
                    soname = Some(sn.to_string());
                } else if part == "-soname" && j + 1 < parts.len() {
                    j += 1;
                    soname = Some(parts[j].to_string());
                } else if part == "-soname" {
                    pending_soname = true;
                } else if let Some(rp) = part.strip_prefix("-rpath=") {
                    rpath_entries.push(rp.to_string());
                } else if part == "-rpath" && j + 1 < parts.len() {
                    j += 1;
                    rpath_entries.push(parts[j].to_string());
                } else if part == "-rpath" {
                    pending_rpath = true;
                } else if part == "--enable-new-dtags" {
                    use_runpath = true;
                } else if part == "--disable-new-dtags" {
                    use_runpath = false;
                } else if let Some(lpath) = part.strip_prefix("-L") {
                    extra_lib_paths.push(lpath.to_string());
                } else if let Some(lib) = part.strip_prefix("-l") {
                    ordered_items.push((lib.to_string(), true, whole_archive));
                } else if part == "--whole-archive" {
                    whole_archive = true;
                } else if part == "--no-whole-archive" {
                    whole_archive = false;
                }
                j += 1;
            }
        } else if arg == "-shared" || arg == "-nostdlib" || arg == "-o" {
            if arg == "-o" { i += 1; } // skip output path
        } else if !arg.starts_with('-') && Path::new(arg).exists() {
            ordered_items.push((arg.to_string(), false, whole_archive));
        }
        i += 1;
    }

    // Load user object files (from the compiler driver, before user_args)
    for path in object_files {
        load_file(path, &mut objects, &mut globals, &mut needed_sonames, &lib_path_strings, false)?;
    }

    let mut all_lib_paths: Vec<String> = extra_lib_paths;
    all_lib_paths.extend(lib_path_strings.iter().cloned());

    // Load ordered items (bare files and -l libraries) preserving --whole-archive state
    let mut libs_to_load_later: Vec<(String, bool)> = Vec::new();
    for (item, is_lib, wa) in &ordered_items {
        if *is_lib {
            libs_to_load_later.push((item.clone(), *wa));
        } else {
            load_file(item, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, *wa)?;
        }
    }

    // Resolve -l libraries
    if !libs_to_load_later.is_empty() {
        let mut lib_paths_resolved: Vec<(String, bool)> = Vec::new();
        for (lib_name, wa) in &libs_to_load_later {
            if let Some(lib_path) = linker_common::resolve_lib(lib_name, &all_lib_paths, false) {
                if !lib_paths_resolved.iter().any(|(p, _)| p == &lib_path) {
                    lib_paths_resolved.push((lib_path, *wa));
                }
            } else {
                return Err(format!("cannot find -l{}: No such file or directory", lib_name));
            }
        }
        // Track which whole-archive libraries have been fully loaded to avoid
        // re-adding all members on subsequent iterations of the group loop.
        let mut whole_archive_loaded: HashSet<String> = HashSet::new();
        let mut changed = true;
        while changed {
            changed = false;
            let prev_count = objects.len();
            for (lib_path, wa) in &lib_paths_resolved {
                if *wa && whole_archive_loaded.contains(lib_path) {
                    continue; // Already loaded all members
                }
                load_file(lib_path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, *wa)?;
                if *wa {
                    whole_archive_loaded.insert(lib_path.clone());
                }
            }
            if objects.len() != prev_count { changed = true; }
        }
    }

    // Resolve implicit libraries (e.g. libgcc.a) to provide compiler runtime
    // functions like __udivti3 that may be referenced by user code.
    if !needed_libs.is_empty() {
        let mut implicit_paths: Vec<String> = Vec::new();
        for lib_name in needed_libs {
            if let Some(lib_path) = linker_common::resolve_lib(lib_name, &all_lib_paths, false) {
                if !implicit_paths.contains(&lib_path) {
                    implicit_paths.push(lib_path);
                }
            }
        }
        let mut changed = true;
        while changed {
            changed = false;
            let prev_count = objects.len();
            for lib_path in &implicit_paths {
                load_file(lib_path, &mut objects, &mut globals, &mut needed_sonames, &all_lib_paths, false)?;
            }
            if objects.len() != prev_count { changed = true; }
        }
    }

    // Resolve remaining undefined symbols against system libraries (libc, libm,
    // libgcc_s) and add DT_NEEDED entries for any that provide matched symbols.
    let default_libs = ["libc.so.6", "libm.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2"];
    linker_common::resolve_dynamic_symbols_elf64(
        &mut globals, &mut needed_sonames, &all_lib_paths, &default_libs,
    )?;

    // Merge sections (no gc-sections for shared libraries)
    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_map: HashMap<(usize, usize), (usize, u64)> = HashMap::new();
    linker_common::merge_sections_elf64(&objects, &mut output_sections, &mut section_map);

    // Allocate COMMON symbols
    linker_common::allocate_common_symbols_elf64(&mut globals, &mut output_sections);

    // Emit shared library
    emit_shared_library(
        &objects, &mut globals, &mut output_sections, &section_map,
        &needed_sonames, output_path, soname, &rpath_entries, use_runpath,
    )
}
