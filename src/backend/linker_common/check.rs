//! Post-link undefined symbol checking.
//!
//! Validates that all required symbols have been resolved after linking,
//! filtering out dynamic, weak, and linker-defined symbols.

use std::collections::HashMap;

use crate::backend::elf::STB_WEAK;
use super::symbols::{GlobalSymbolOps, is_linker_defined_symbol};

/// Check for undefined symbols in the global symbol table and return an error
/// if any truly undefined symbols are found.
///
/// Filters out dynamic symbols, weak symbols, and linker-defined symbols
/// using the `GlobalSymbolOps` trait methods. `max_report` limits how many
/// symbols are shown in the error message (typically 20).
pub fn check_undefined_symbols_elf64<G: GlobalSymbolOps>(
    globals: &HashMap<String, G>,
    max_report: usize,
) -> Result<(), String> {
    let mut truly_undefined: Vec<&String> = globals.iter()
        .filter(|(name, sym)| {
            !sym.is_defined() && !sym.is_dynamic()
                && (sym.info() >> 4) != STB_WEAK
                && !is_linker_defined_symbol(name)
        })
        .map(|(name, _)| name)
        .collect();
    if truly_undefined.is_empty() {
        return Ok(());
    }
    truly_undefined.sort();
    truly_undefined.truncate(max_report);
    Err(format!(
        "undefined symbols: {}",
        truly_undefined.iter().map(|s| s.as_str()).collect::<Vec<_>>().join(", ")
    ))
}

// ── NSS static linking warnings ─────────────────────────────────────────

/// Known glibc functions that depend on NSS (Name Service Switch).
///
/// These functions internally use `dlopen()` to load NSS service modules at
/// runtime, which can fail when the application is linked statically with
/// `-static`. The list covers resolver, passwd/group, host, and service
/// lookup families including their reentrant (`_r`) variants.
const NSS_DEPENDENT_FUNCTIONS: &[&str] = &[
    "getaddrinfo",
    "gethostbyaddr",
    "gethostbyaddr_r",
    "gethostbyname",
    "gethostbyname2",
    "gethostbyname2_r",
    "gethostbyname_r",
    "getgrent",
    "getgrgid",
    "getgrgid_r",
    "getgrnam",
    "getgrnam_r",
    "getnameinfo",
    "getpwent",
    "getpwnam",
    "getpwnam_r",
    "getpwuid",
    "getpwuid_r",
    "getservbyname",
    "getservbyname_r",
    "getservbyport",
    "getservbyport_r",
];

/// Check for NSS-dependent functions in statically linked binaries.
///
/// When `-static` is used, certain glibc functions (`getaddrinfo`, `getpwnam`,
/// etc.) may fail at runtime because NSS uses `dlopen()` to load service
/// modules. This emits a non-fatal warning to stderr for each such function
/// found as a defined symbol in the link (meaning it was resolved from a
/// static libc archive).
///
/// If `is_static` is `false`, returns immediately with an empty list.
///
/// Returns the list of NSS-dependent function names that triggered warnings,
/// sorted alphabetically for deterministic output.
pub fn check_nss_static_warning<G: GlobalSymbolOps>(
    globals: &HashMap<String, G>,
    is_static: bool,
) -> Vec<String> {
    if !is_static {
        return Vec::new();
    }

    // Collect NSS symbols that are DEFINED in the global symbol table.
    // A defined symbol means it was resolved from a static libc archive;
    // undefined references would already fail at link time and are not
    // our concern here.
    let mut warned: Vec<String> = NSS_DEPENDENT_FUNCTIONS
        .iter()
        .filter(|&&func_name| {
            globals.get(func_name).map_or(false, |sym| sym.is_defined())
        })
        .map(|&func_name| func_name.to_string())
        .collect();

    // Sort for deterministic output across runs.
    warned.sort();

    for name in &warned {
        eprintln!(
            "warning: using '{}' in statically linked applications \
             requires at runtime the shared libraries from the glibc \
             version used for linking",
            name
        );
    }

    warned
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Minimal mock implementation of GlobalSymbolOps for testing.
    #[derive(Clone)]
    struct MockSymbol {
        defined: bool,
        dynamic: bool,
        sym_info: u8,
    }

    impl MockSymbol {
        fn defined() -> Self {
            MockSymbol { defined: true, dynamic: false, sym_info: 0x12 } // STB_GLOBAL
        }
        fn undefined() -> Self {
            MockSymbol { defined: false, dynamic: false, sym_info: 0x10 } // STB_GLOBAL, undef
        }
    }

    impl GlobalSymbolOps for MockSymbol {
        fn is_defined(&self) -> bool { self.defined }
        fn is_dynamic(&self) -> bool { self.dynamic }
        fn info(&self) -> u8 { self.sym_info }
        fn section_idx(&self) -> u16 { 0 }
        fn value(&self) -> u64 { 0 }
        fn size(&self) -> u64 { 0 }
        fn new_defined(_obj_idx: usize, _sym: &crate::backend::linker_common::Elf64Symbol) -> Self {
            MockSymbol::defined()
        }
        fn new_common(_obj_idx: usize, _sym: &crate::backend::linker_common::Elf64Symbol) -> Self {
            MockSymbol::defined()
        }
        fn new_undefined(_sym: &crate::backend::linker_common::Elf64Symbol) -> Self {
            MockSymbol::undefined()
        }
        fn set_common_bss(&mut self, _bss_offset: u64) {}
        fn new_dynamic(_dsym: &crate::backend::linker_common::DynSymbol, _soname: &str) -> Self {
            MockSymbol { defined: true, dynamic: true, sym_info: 0x12 }
        }
    }

    #[test]
    fn nss_warning_returns_empty_when_not_static() {
        let mut globals = HashMap::new();
        globals.insert("getaddrinfo".to_string(), MockSymbol::defined());
        let result = check_nss_static_warning(&globals, false);
        assert!(result.is_empty(), "Should return empty when is_static=false");
    }

    #[test]
    fn nss_warning_returns_empty_when_no_nss_symbols() {
        let mut globals = HashMap::new();
        globals.insert("main".to_string(), MockSymbol::defined());
        globals.insert("printf".to_string(), MockSymbol::defined());
        let result = check_nss_static_warning(&globals, true);
        assert!(result.is_empty(), "Should return empty when no NSS symbols present");
    }

    #[test]
    fn nss_warning_detects_defined_nss_symbols() {
        let mut globals = HashMap::new();
        globals.insert("getaddrinfo".to_string(), MockSymbol::defined());
        globals.insert("getpwnam".to_string(), MockSymbol::defined());
        globals.insert("main".to_string(), MockSymbol::defined());
        let result = check_nss_static_warning(&globals, true);
        assert_eq!(result.len(), 2);
        assert!(result.contains(&"getaddrinfo".to_string()));
        assert!(result.contains(&"getpwnam".to_string()));
    }

    #[test]
    fn nss_warning_ignores_undefined_nss_symbols() {
        let mut globals = HashMap::new();
        globals.insert("getaddrinfo".to_string(), MockSymbol::undefined());
        globals.insert("gethostbyname".to_string(), MockSymbol::defined());
        let result = check_nss_static_warning(&globals, true);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0], "gethostbyname");
    }

    #[test]
    fn nss_warning_output_is_sorted() {
        let mut globals = HashMap::new();
        globals.insert("getpwuid".to_string(), MockSymbol::defined());
        globals.insert("getaddrinfo".to_string(), MockSymbol::defined());
        globals.insert("gethostbyname".to_string(), MockSymbol::defined());
        let result = check_nss_static_warning(&globals, true);
        assert_eq!(result, vec!["getaddrinfo", "gethostbyname", "getpwuid"]);
    }

    #[test]
    fn nss_warning_empty_globals() {
        let globals: HashMap<String, MockSymbol> = HashMap::new();
        let result = check_nss_static_warning(&globals, true);
        assert!(result.is_empty());
    }

    #[test]
    fn nss_warning_all_nss_functions_detected() {
        let mut globals = HashMap::new();
        // Add all NSS functions as defined
        for &name in super::NSS_DEPENDENT_FUNCTIONS {
            globals.insert(name.to_string(), MockSymbol::defined());
        }
        let result = check_nss_static_warning(&globals, true);
        assert_eq!(result.len(), super::NSS_DEPENDENT_FUNCTIONS.len());
    }

    // Verify that existing check_undefined_symbols_elf64 still works correctly.
    #[test]
    fn existing_check_undefined_returns_ok_for_all_defined() {
        let mut globals = HashMap::new();
        globals.insert("main".to_string(), MockSymbol::defined());
        globals.insert("printf".to_string(), MockSymbol::defined());
        let result = check_undefined_symbols_elf64(&globals, 20);
        assert!(result.is_ok());
    }

    #[test]
    fn existing_check_undefined_returns_err_for_undefined() {
        let mut globals = HashMap::new();
        globals.insert("missing_func".to_string(), MockSymbol::undefined());
        let result = check_undefined_symbols_elf64(&globals, 20);
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.contains("missing_func"), "Error should mention the undefined symbol");
    }
}
