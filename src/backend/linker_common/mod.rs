//! Shared linker infrastructure for all backends.
//!
//! Split into focused submodules:
//!
//! - `types`: ELF64 object file types (Elf64Section, Elf64Symbol, Elf64Rela, Elf64Object, DynSymbol)
//! - `parse_object`: ELF64 relocatable object file parser
//! - `parse_shared`: Shared library (.so) symbol parsing and SONAME extraction
//! - `section_map`: Input-to-output section name mapping
//! - `dynstr`: Dynamic string table builder for .dynstr emission
//! - `hash`: GNU and SysV ELF hash functions
//! - `symbols`: InputSection, OutputSection, GlobalSymbolOps trait, linker-defined symbols
//! - `merge`: Section merging and common symbol allocation
//! - `dynamic`: Dynamic symbol matching, library loading, and symbol registration
//! - `archive`: Archive loading and generic file dispatch
//! - `resolve_lib`: Library name resolution helper
//! - `write`: ELF64 binary emission helpers (section/program headers, alignment)
//! - `args`: Shared linker argument parsing
//! - `check`: Post-link undefined symbol checking
//! - `eh_frame`: .eh_frame_hdr builder for stack unwinding
//! - `gc_sections`: Garbage collection (`--gc-sections`) for ELF64 linkers
//! - `linker_script`: GNU ld-compatible linker script parser (SECTIONS, MEMORY, ENTRY, PROVIDE, KEEP)
//!
//! This module extracts the duplicated linker code that was copied across x86,
//! ARM, RISC-V, and (partially) i686 backends. It provides:
//!
//! - **ELF64 object parser**: `parse_elf64_object()` replaces near-identical
//!   `parse_object()` functions in x86, ARM, and RISC-V linkers.
//! - **Shared library parser**: `parse_shared_library_symbols()` and `parse_soname()`
//!   for extracting dynamic symbols from .so files.
//! - **Dynamic symbol matching**: `match_shared_library_dynsyms()`,
//!   `load_shared_library_elf64()`, and `resolve_dynamic_symbols_elf64()` for
//!   matching undefined globals against shared library exports with WEAK alias
//!   detection and as-needed semantics.
//! - **Linker-defined symbols**: `LINKER_DEFINED_SYMBOLS` constant and
//!   `is_linker_defined_symbol()` for the superset of symbols the linker
//!   provides during layout (used by all 4 backends).
//! - **Archive loading**: `load_archive_members()` and `member_resolves_undefined()`
//!   for iterative archive resolution (the --start-group algorithm).
//! - **Section mapping**: `map_section_name()` for input-to-output section mapping.
//! - **DynStrTab**: Dynamic string table builder for dynamic linking.
//! - **GNU hash**: `build_gnu_hash()` for .gnu.hash section generation.
//! - **ELF64 writing helpers**: `write_elf64_shdr()`, `write_elf64_phdr()`,
//!   `write_elf64_phdr_at()`, `align_up_64()`, `pad_to()` for binary emission.
//! - **Argument parsing**: `parse_linker_args()` and `LinkerArgs` for shared
//!   `-Wl,` flag parsing across backends.
//! - **Undefined symbol checking**: `check_undefined_symbols_elf64()` for
//!   post-link validation via the `GlobalSymbolOps` trait.
//! - **Linker script parsing**: `parse_linker_script()` and `LinkerScript` for
//!   `-T script.ld` section placement, MEMORY regions, ENTRY/PROVIDE symbols,
//!   and KEEP directives consumed by per-architecture linkers.
//!
//! Each backend linker still handles its own:
//! - Architecture-specific relocation application
//! - PLT/GOT layout (different instruction sequences per arch)
//! - ELF header emission (different e_machine, base addresses)
//! - Dynamic linking specifics (version tables, etc.)

// ── Submodule declarations ──────────────────────────────────────────────

mod types;
mod parse_object;
mod parse_shared;
mod section_map;
mod dynstr;
mod hash;
mod symbols;
mod merge;
mod dynamic;
mod archive;
mod resolve_lib;
mod write;
mod args;
mod check;
mod eh_frame;
mod gc_sections;
pub(crate) mod linker_script;

// ── Re-exports ──────────────────────────────────────────────────────────
//
// Re-export all public items at the linker_common:: level so that external
// callers see no change from the previous flat-file layout.

// types.rs
pub use types::{Elf64Section, Elf64Symbol, Elf64Rela, Elf64Object, DynSymbol};

// parse_object.rs
pub use parse_object::parse_elf64_object;

// parse_shared.rs
pub use parse_shared::{parse_shared_library_symbols, parse_soname};

// dynstr.rs
pub use dynstr::DynStrTab;

// hash.rs
pub use hash::{gnu_hash, sysv_hash};

// symbols.rs
pub use symbols::{
    OutputSection, GlobalSymbolOps,
    is_linker_defined_symbol,
    is_valid_c_identifier_for_section, resolve_start_stop_symbols,
    resolve_provide_symbols, resolve_entry_symbol,
};

// merge.rs
pub use merge::{
    merge_sections_elf64, merge_sections_elf64_gc, allocate_common_symbols_elf64,
    merge_sections_with_script, is_section_kept,
};

// dynamic.rs
pub use dynamic::{
    load_shared_library_elf64,
    resolve_dynamic_symbols_elf64, register_symbols_elf64,
};

// archive.rs
pub use archive::{load_archive_elf64, load_thin_archive_elf64};

// resolve_lib.rs
pub use resolve_lib::resolve_lib;

// write.rs
pub use write::{write_elf64_shdr, write_elf64_phdr, write_elf64_phdr_at, align_up_64, pad_to};

// args.rs
pub use args::parse_linker_args;

// check.rs
pub use check::{check_undefined_symbols_elf64, check_nss_static_warning};

// eh_frame.rs
pub use eh_frame::{count_eh_frame_fdes, build_eh_frame_hdr};

// gc_sections.rs
pub use gc_sections::gc_collect_sections_elf64;

// linker_script.rs
pub use linker_script::{
    LinkerScript, ScriptSection, SymbolExpr,
    parse_linker_script,
};
