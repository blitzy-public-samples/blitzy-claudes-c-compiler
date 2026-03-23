//! Section merging and common symbol allocation for ELF64 linkers.
//!
//! Groups input sections by mapped name, computes output offsets with proper
//! alignment, sorts output sections by permission profile, and allocates
//! SHN_COMMON symbols into `.bss`.

use std::collections::{HashMap, HashSet};

use crate::backend::elf::{
    SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_REL,
    SHT_NOBITS, SHT_GROUP,
    SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR, SHF_TLS, SHF_EXCLUDE,
    SHN_COMMON,
};
use super::types::Elf64Object;
use super::symbols::{InputSection, OutputSection, GlobalSymbolOps};
use super::section_map::map_section_name;

/// Merge input sections from all objects into output sections.
///
/// Groups input sections by mapped name (e.g., `.text.foo` -> `.text`),
/// computes output offsets with proper alignment, and sorts output sections
/// by permission profile: RO -> Exec -> RW(progbits) -> RW(nobits).
pub fn merge_sections_elf64(
    objects: &[Elf64Object], output_sections: &mut Vec<OutputSection>,
    section_map: &mut HashMap<(usize, usize), (usize, u64)>,
) {
    let no_dead = HashSet::new();
    merge_sections_elf64_gc(objects, output_sections, section_map, &no_dead);
}

/// Merge input sections into output sections, optionally skipping dead sections.
///
/// When `dead_sections` is non-empty (from --gc-sections), sections in the set
/// are excluded from the output, effectively garbage-collecting unreferenced code.
pub fn merge_sections_elf64_gc(
    objects: &[Elf64Object], output_sections: &mut Vec<OutputSection>,
    section_map: &mut HashMap<(usize, usize), (usize, u64)>,
    dead_sections: &HashSet<(usize, usize)>,
) {
    let mut output_map: HashMap<String, usize> = HashMap::new();

    for obj_idx in 0..objects.len() {
        for sec_idx in 0..objects[obj_idx].sections.len() {
            let sec = &objects[obj_idx].sections[sec_idx];
            if sec.flags & SHF_ALLOC == 0 { continue; }
            if matches!(sec.sh_type, SHT_NULL | SHT_STRTAB | SHT_SYMTAB | SHT_RELA | SHT_REL | SHT_GROUP) { continue; }
            if sec.flags & SHF_EXCLUDE != 0 { continue; }
            if !dead_sections.is_empty() && dead_sections.contains(&(obj_idx, sec_idx)) { continue; }

            let output_name = map_section_name(&sec.name).to_string();
            let alignment = sec.addralign.max(1);

            let out_idx = if let Some(&idx) = output_map.get(&output_name) {
                if alignment > output_sections[idx].alignment {
                    output_sections[idx].alignment = alignment;
                }
                idx
            } else {
                let idx = output_sections.len();
                output_map.insert(output_name.clone(), idx);
                output_sections.push(OutputSection {
                    name: output_name, sh_type: sec.sh_type, flags: sec.flags,
                    alignment, inputs: Vec::new(), data: Vec::new(),
                    addr: 0, file_offset: 0, mem_size: 0,
                });
                idx
            };

            if sec.sh_type == SHT_PROGBITS { output_sections[out_idx].sh_type = SHT_PROGBITS; }
            output_sections[out_idx].flags |= sec.flags & (SHF_WRITE | SHF_EXECINSTR | SHF_ALLOC | SHF_TLS);
            output_sections[out_idx].inputs.push(InputSection {
                object_idx: obj_idx, section_idx: sec_idx, output_offset: 0, size: sec.size,
            });
        }
    }

    for out_sec in output_sections.iter_mut() {
        let mut off: u64 = 0;
        for input in &mut out_sec.inputs {
            let a = objects[input.object_idx].sections[input.section_idx].addralign.max(1);
            off = (off + a - 1) & !(a - 1);
            input.output_offset = off;
            off += input.size;
        }
        out_sec.mem_size = off;
    }

    for (out_idx, out_sec) in output_sections.iter().enumerate() {
        for input in &out_sec.inputs {
            section_map.insert((input.object_idx, input.section_idx), (out_idx, input.output_offset));
        }
    }

    // Sort: RO -> Exec -> RW(progbits) -> RW(nobits)
    let len = output_sections.len();
    let mut opts: Vec<Option<OutputSection>> = output_sections.drain(..).map(Some).collect();
    let mut sort_indices: Vec<usize> = (0..len).collect();
    sort_indices.sort_by_key(|&i| {
        let sec = opts[i].as_ref().unwrap();
        let is_exec = sec.flags & SHF_EXECINSTR != 0;
        let is_write = sec.flags & SHF_WRITE != 0;
        let is_nobits = sec.sh_type == SHT_NOBITS;
        if is_exec { (1u32, is_nobits as u32) }
        else if !is_write { (0, is_nobits as u32) }
        else { (2, is_nobits as u32) }
    });

    let mut index_remap: HashMap<usize, usize> = HashMap::new();
    for (new_idx, &old_idx) in sort_indices.iter().enumerate() {
        index_remap.insert(old_idx, new_idx);
    }
    for &old_idx in &sort_indices {
        output_sections.push(opts[old_idx].take().unwrap());
    }

    let old_map: Vec<_> = section_map.drain().collect();
    for ((obj_idx, sec_idx), (old_out_idx, off)) in old_map {
        if let Some(&new_out_idx) = index_remap.get(&old_out_idx) {
            section_map.insert((obj_idx, sec_idx), (new_out_idx, off));
        }
    }
}

/// Allocate SHN_COMMON symbols into the .bss output section.
pub fn allocate_common_symbols_elf64<G: GlobalSymbolOps>(
    globals: &mut HashMap<String, G>, output_sections: &mut Vec<OutputSection>,
) {
    let common_syms: Vec<(String, u64, u64)> = globals.iter()
        .filter(|(_, sym)| sym.section_idx() == SHN_COMMON && sym.is_defined())
        .map(|(name, sym)| (name.clone(), sym.value().max(1), sym.size())).collect();
    if common_syms.is_empty() { return; }

    let bss_idx = output_sections.iter().position(|s| s.name == ".bss").unwrap_or_else(|| {
        let idx = output_sections.len();
        output_sections.push(OutputSection {
            name: ".bss".to_string(), sh_type: SHT_NOBITS,
            flags: SHF_ALLOC | SHF_WRITE, alignment: 1,
            inputs: Vec::new(), data: Vec::new(),
            addr: 0, file_offset: 0, mem_size: 0,
        });
        idx
    });

    let mut bss_off = output_sections[bss_idx].mem_size;
    for (name, alignment, size) in &common_syms {
        let a = (*alignment).max(1);
        bss_off = (bss_off + a - 1) & !(a - 1);
        if let Some(sym) = globals.get_mut(name) {
            sym.set_common_bss(bss_off);
        }
        if *alignment > output_sections[bss_idx].alignment {
            output_sections[bss_idx].alignment = *alignment;
        }
        bss_off += size;
    }
    output_sections[bss_idx].mem_size = bss_off;
}

// ── Linker Script Section Support ───────────────────────────────────────

/// Extract the section name pattern from a linker script input specification.
///
/// Handles patterns in the form `*(.text)`, `*(.text.*)`, or bare section
/// names like `.text`.  For `*(.text)` returns `.text`; for `.text` returns
/// `.text` unchanged.
fn extract_section_pattern(spec: &str) -> &str {
    if let Some(start) = spec.find('(') {
        if let Some(end) = spec.rfind(')') {
            if start < end {
                return &spec[start + 1..end];
            }
        }
    }
    spec
}

/// Match a section name against a linker script wildcard pattern.
///
/// Supports `*` (matches any sequence of characters) and `?` (matches exactly
/// one character).  All other characters match literally.  Used by SECTIONS
/// directives to select input sections for output section placement.
///
/// Examples:
/// - `*` matches any section name
/// - `.text*` matches `.text`, `.text.foo`, `.text.bar.baz`
/// - `.rodata.*.cst?` matches `.rodata.foo.cst4`
fn match_section_wildcard(pattern: &str, name: &str) -> bool {
    let p = pattern.as_bytes();
    let n = name.as_bytes();
    let plen = p.len();
    let nlen = n.len();
    let mut pi = 0usize;
    let mut ni = 0usize;
    // Tracks the position of the most recent `*` in the pattern and the
    // corresponding name offset used when backtracking.
    let mut star_pi = usize::MAX;
    let mut star_ni = 0usize;

    while ni < nlen {
        if pi < plen && (p[pi] == b'?' || p[pi] == n[ni]) {
            // Exact character match or single-character wildcard.
            pi += 1;
            ni += 1;
        } else if pi < plen && p[pi] == b'*' {
            // Record the star position for backtracking.  The star initially
            // matches zero characters; advance the pattern pointer past it.
            star_pi = pi;
            star_ni = ni;
            pi += 1;
        } else if star_pi != usize::MAX {
            // Mismatch after a previous star: backtrack and let the star
            // consume one more character from the name.
            pi = star_pi + 1;
            star_ni += 1;
            ni = star_ni;
        } else {
            return false;
        }
    }

    // Consume any trailing `*` characters in the pattern (they match empty).
    while pi < plen && p[pi] == b'*' {
        pi += 1;
    }

    pi == plen
}

/// Check if an input section should be retained due to KEEP() directives.
///
/// Returns true if the section name matches any of the keep patterns, meaning
/// it should NOT be garbage-collected even with `--gc-sections`.  Patterns may
/// be in linker script form (e.g., `*(.init_array)`) or bare section name
/// wildcards (e.g., `.init_array`).
pub fn is_section_kept(section_name: &str, keep_patterns: &[String]) -> bool {
    keep_patterns.iter().any(|pattern| {
        let section_pat = extract_section_pattern(pattern);
        match_section_wildcard(section_pat, section_name)
    })
}

/// Merge input sections respecting linker script SECTIONS directives.
///
/// When a linker script specifies section ordering and placement, this function
/// overrides the default section sorting (RO → Exec → RW → BSS) with the
/// script-specified order.
///
/// `script_sections` is a list of (output\_section\_name, input\_patterns,
/// address\_opt) tuples from the parsed linker script SECTIONS block:
/// - `output_section_name`: e.g., `".text"`, `".rodata"`, `".data"`
/// - `input_patterns`: wildcard patterns for input sections,
///   e.g., `["*(.text)", "*(.text.*)"]`
/// - `address_opt`: optional fixed address for this output section
///
/// `keep_patterns` is a list of wildcard patterns from KEEP() directives that
/// should be retained even when `--gc-sections` is active.
///
/// Sections matched by the script are placed in the specified order without
/// the default RO→Exec→RW sort.  Unmatched remainder sections fall back to
/// the standard permission-profile sort used by [`merge_sections_elf64_gc`].
pub fn merge_sections_with_script<G: GlobalSymbolOps>(
    objects: &[Elf64Object],
    output_sections: &mut Vec<OutputSection>,
    section_map: &mut HashMap<(usize, usize), (usize, u64)>,
    dead_sections: &HashSet<(usize, usize)>,
    script_sections: &[(String, Vec<String>, Option<u64>)],
    keep_patterns: &[String],
) {
    let mut placed: HashSet<(usize, usize)> = HashSet::new();

    // ── Pass 1: Create output sections from linker script in order ──────
    for (out_name, input_patterns, addr_opt) in script_sections {
        let out_idx = output_sections.len();
        output_sections.push(OutputSection {
            name: out_name.clone(),
            sh_type: SHT_PROGBITS,
            flags: SHF_ALLOC,
            alignment: 1,
            inputs: Vec::new(),
            data: Vec::new(),
            addr: addr_opt.unwrap_or(0),
            file_offset: 0,
            mem_size: 0,
        });

        for obj_idx in 0..objects.len() {
            for sec_idx in 0..objects[obj_idx].sections.len() {
                if placed.contains(&(obj_idx, sec_idx)) { continue; }
                let sec = &objects[obj_idx].sections[sec_idx];
                if sec.flags & SHF_ALLOC == 0 { continue; }
                if matches!(sec.sh_type,
                    SHT_NULL | SHT_STRTAB | SHT_SYMTAB
                    | SHT_RELA | SHT_REL | SHT_GROUP) { continue; }
                if sec.flags & SHF_EXCLUDE != 0 { continue; }

                // KEEP handling: retained sections bypass the dead-section check.
                let kept = is_section_kept(&sec.name, keep_patterns);
                if !kept
                    && !dead_sections.is_empty()
                    && dead_sections.contains(&(obj_idx, sec_idx))
                {
                    continue;
                }

                // Check if section name matches any input pattern for this entry.
                let matches_pattern = input_patterns.iter().any(|pat| {
                    let section_pat = extract_section_pattern(pat);
                    match_section_wildcard(section_pat, &sec.name)
                });
                if !matches_pattern { continue; }

                placed.insert((obj_idx, sec_idx));
                let alignment = sec.addralign.max(1);
                if alignment > output_sections[out_idx].alignment {
                    output_sections[out_idx].alignment = alignment;
                }
                if sec.sh_type == SHT_PROGBITS {
                    output_sections[out_idx].sh_type = SHT_PROGBITS;
                }
                output_sections[out_idx].flags |=
                    sec.flags & (SHF_WRITE | SHF_EXECINSTR | SHF_ALLOC | SHF_TLS);
                output_sections[out_idx].inputs.push(InputSection {
                    object_idx: obj_idx, section_idx: sec_idx,
                    output_offset: 0, size: sec.size,
                });
            }
        }
    }

    let script_count = output_sections.len();

    // ── Pass 2: Merge remaining unmatched input sections (default logic) ─
    let mut output_map: HashMap<String, usize> = HashMap::new();

    for obj_idx in 0..objects.len() {
        for sec_idx in 0..objects[obj_idx].sections.len() {
            if placed.contains(&(obj_idx, sec_idx)) { continue; }
            let sec = &objects[obj_idx].sections[sec_idx];
            if sec.flags & SHF_ALLOC == 0 { continue; }
            if matches!(sec.sh_type,
                SHT_NULL | SHT_STRTAB | SHT_SYMTAB
                | SHT_RELA | SHT_REL | SHT_GROUP) { continue; }
            if sec.flags & SHF_EXCLUDE != 0 { continue; }

            // KEEP handling for remainder sections.
            let kept = is_section_kept(&sec.name, keep_patterns);
            if !kept
                && !dead_sections.is_empty()
                && dead_sections.contains(&(obj_idx, sec_idx))
            {
                continue;
            }

            let output_name = map_section_name(&sec.name).to_string();
            let alignment = sec.addralign.max(1);

            let out_idx = if let Some(&idx) = output_map.get(&output_name) {
                if alignment > output_sections[idx].alignment {
                    output_sections[idx].alignment = alignment;
                }
                idx
            } else {
                let idx = output_sections.len();
                output_map.insert(output_name.clone(), idx);
                output_sections.push(OutputSection {
                    name: output_name, sh_type: sec.sh_type, flags: sec.flags,
                    alignment, inputs: Vec::new(), data: Vec::new(),
                    addr: 0, file_offset: 0, mem_size: 0,
                });
                idx
            };

            if sec.sh_type == SHT_PROGBITS {
                output_sections[out_idx].sh_type = SHT_PROGBITS;
            }
            output_sections[out_idx].flags |=
                sec.flags & (SHF_WRITE | SHF_EXECINSTR | SHF_ALLOC | SHF_TLS);
            output_sections[out_idx].inputs.push(InputSection {
                object_idx: obj_idx, section_idx: sec_idx,
                output_offset: 0, size: sec.size,
            });
        }
    }

    // ── Compute per-input offsets with alignment for all output sections ──
    for out_sec in output_sections.iter_mut() {
        let mut off: u64 = 0;
        for input in &mut out_sec.inputs {
            let a = objects[input.object_idx].sections[input.section_idx]
                .addralign
                .max(1);
            off = (off + a - 1) & !(a - 1);
            input.output_offset = off;
            off += input.size;
        }
        out_sec.mem_size = off;
    }

    // ── Build section map (pre-sort) ─────────────────────────────────────
    for (out_idx, out_sec) in output_sections.iter().enumerate() {
        for input in &out_sec.inputs {
            section_map.insert(
                (input.object_idx, input.section_idx),
                (out_idx, input.output_offset),
            );
        }
    }

    // ── Sort only the non-script remainder by permission profile ─────────
    // Script-placed sections (indices 0..script_count) retain their order.
    // The remainder is sorted RO → Exec → RW(progbits) → RW(nobits).
    if script_count < output_sections.len() {
        let remainder_len = output_sections.len() - script_count;
        let mut remainder: Vec<Option<OutputSection>> =
            output_sections.drain(script_count..).map(Some).collect();
        let mut sort_indices: Vec<usize> = (0..remainder_len).collect();
        sort_indices.sort_by_key(|&i| {
            let sec = remainder[i].as_ref().unwrap();
            let is_exec = sec.flags & SHF_EXECINSTR != 0;
            let is_write = sec.flags & SHF_WRITE != 0;
            let is_nobits = sec.sh_type == SHT_NOBITS;
            if is_exec { (1u32, is_nobits as u32) }
            else if !is_write { (0, is_nobits as u32) }
            else { (2, is_nobits as u32) }
        });

        let mut index_remap: HashMap<usize, usize> = HashMap::new();
        for (new_offset, &old_offset) in sort_indices.iter().enumerate() {
            index_remap.insert(script_count + old_offset, script_count + new_offset);
        }
        for &old_offset in &sort_indices {
            output_sections.push(remainder[old_offset].take().unwrap());
        }

        // Remap section_map entries for the sorted remainder sections.
        let old_map: Vec<_> = section_map.drain().collect();
        for ((obj_idx, sec_idx), (old_out_idx, off)) in old_map {
            if old_out_idx < script_count {
                // Script-placed sections keep their original indices.
                section_map.insert((obj_idx, sec_idx), (old_out_idx, off));
            } else if let Some(&new_out_idx) = index_remap.get(&old_out_idx) {
                section_map.insert((obj_idx, sec_idx), (new_out_idx, off));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // ── match_section_wildcard tests ────────────────────────────────────

    #[test]
    fn wildcard_star_matches_anything() {
        assert!(match_section_wildcard("*", ".text"));
        assert!(match_section_wildcard("*", ".rodata.foo.cst4"));
        assert!(match_section_wildcard("*", ""));
    }

    #[test]
    fn wildcard_exact_match() {
        assert!(match_section_wildcard(".text", ".text"));
        assert!(!match_section_wildcard(".text", ".data"));
        assert!(!match_section_wildcard(".text", ".text.foo"));
    }

    #[test]
    fn wildcard_suffix_star() {
        assert!(match_section_wildcard(".text*", ".text"));
        assert!(match_section_wildcard(".text*", ".text.foo"));
        assert!(match_section_wildcard(".text*", ".text.bar.baz"));
        assert!(!match_section_wildcard(".text*", ".data"));
    }

    #[test]
    fn wildcard_middle_star() {
        assert!(match_section_wildcard(".rodata.*.cst4", ".rodata.foo.cst4"));
        assert!(match_section_wildcard(".rodata.*.cst4", ".rodata.bar.cst4"));
        assert!(!match_section_wildcard(".rodata.*.cst4", ".rodata.foo.cst8"));
    }

    #[test]
    fn wildcard_question_mark() {
        assert!(match_section_wildcard(".rodata.*.cst?", ".rodata.foo.cst4"));
        assert!(match_section_wildcard(".rodata.*.cst?", ".rodata.foo.cst8"));
        assert!(!match_section_wildcard(".rodata.*.cst?", ".rodata.foo.cst42"));
    }

    #[test]
    fn wildcard_empty_pattern() {
        assert!(match_section_wildcard("", ""));
        assert!(!match_section_wildcard("", ".text"));
    }

    #[test]
    fn wildcard_multiple_stars() {
        assert!(match_section_wildcard("*.*.*", ".a.b.c"));
        assert!(match_section_wildcard("*.*.*", ".."));
        assert!(!match_section_wildcard("*.*.*", "ab"));
    }

    #[test]
    fn wildcard_consecutive_stars() {
        assert!(match_section_wildcard("**", ".text"));
        assert!(match_section_wildcard(".**", ".text"));
    }

    // ── extract_section_pattern tests ───────────────────────────────────

    #[test]
    fn extract_pattern_with_parens() {
        assert_eq!(extract_section_pattern("*(.text)"), ".text");
        assert_eq!(extract_section_pattern("*(.text.*)"), ".text.*");
        assert_eq!(extract_section_pattern("foo(.bar)"), ".bar");
    }

    #[test]
    fn extract_pattern_bare() {
        assert_eq!(extract_section_pattern(".text"), ".text");
        assert_eq!(extract_section_pattern("*"), "*");
    }

    #[test]
    fn extract_pattern_malformed() {
        // No closing paren — return the whole spec
        assert_eq!(extract_section_pattern("*(.text"), "*(.text");
        // Empty parens
        assert_eq!(extract_section_pattern("*()"), "");
    }

    // ── is_section_kept tests ───────────────────────────────────────────

    #[test]
    fn kept_with_linker_script_pattern() {
        assert!(is_section_kept(
            ".init_array",
            &["*(.init_array)".to_string()]
        ));
    }

    #[test]
    fn kept_with_wildcard_pattern() {
        assert!(is_section_kept(
            ".init_array.00001",
            &["*(.init_array*)".to_string()]
        ));
    }

    #[test]
    fn kept_no_match() {
        assert!(!is_section_kept(
            ".text",
            &["*(.init_array)".to_string()]
        ));
    }

    #[test]
    fn kept_empty_patterns() {
        assert!(!is_section_kept(".text", &[]));
    }

    #[test]
    fn kept_multiple_patterns() {
        let patterns = vec![
            "*(.init_array)".to_string(),
            "*(.fini_array)".to_string(),
        ];
        assert!(is_section_kept(".init_array", &patterns));
        assert!(is_section_kept(".fini_array", &patterns));
        assert!(!is_section_kept(".text", &patterns));
    }

    #[test]
    fn kept_bare_section_name() {
        assert!(is_section_kept(".init_array", &[".init_array".to_string()]));
        assert!(!is_section_kept(".init_array.foo", &[".init_array".to_string()]));
    }

    // ── merge_sections_with_script tests ────────────────────────────────

    /// Helper to create a minimal Elf64Object with given sections.
    fn make_object(sections: Vec<(&str, u32, u64, u64, u64)>) -> Elf64Object {
        use crate::backend::elf::SHF_ALLOC;
        let mut secs = Vec::new();
        let mut sec_data = Vec::new();
        for (name, sh_type, flags, size, align) in &sections {
            secs.push(crate::backend::linker_common::types::Elf64Section {
                name_idx: 0,
                name: name.to_string(),
                sh_type: *sh_type,
                flags: *flags | SHF_ALLOC,
                addr: 0,
                offset: 0,
                size: *size,
                link: 0,
                info: 0,
                addralign: *align,
                entsize: 0,
            });
            sec_data.push(vec![0u8; *size as usize]);
        }
        Elf64Object {
            sections: secs,
            symbols: Vec::new(),
            section_data: sec_data,
            relocations: Vec::new(),
            source_name: "test.o".to_string(),
        }
    }

    #[test]
    fn script_merge_basic_ordering() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
            (".data", SHT_PROGBITS, SHF_WRITE, 50, 8),
            (".rodata", SHT_PROGBITS, 0, 30, 4),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        let dead = HashSet::new();

        // Script says: .rodata first, then .text, then .data
        let script_sections = vec![
            (".rodata".to_string(), vec!["*(.rodata)".to_string()], None),
            (".text".to_string(), vec!["*(.text)".to_string()], None),
            (".data".to_string(), vec!["*(.data)".to_string()], None),
        ];

        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &script_sections, &[],
        );

        // Verify script-specified order is preserved.
        assert_eq!(output_sections[0].name, ".rodata");
        assert_eq!(output_sections[1].name, ".text");
        assert_eq!(output_sections[2].name, ".data");
        assert_eq!(output_sections.len(), 3);
    }

    #[test]
    fn script_merge_with_unmatched_remainder() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
            (".data", SHT_PROGBITS, SHF_WRITE, 50, 8),
            (".rodata", SHT_PROGBITS, 0, 30, 4),
            (".bss", SHT_NOBITS, SHF_WRITE, 20, 4),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        let dead = HashSet::new();

        // Script only specifies .text — rest falls through to default sort.
        let script_sections = vec![
            (".text".to_string(), vec!["*(.text)".to_string()], None),
        ];

        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &script_sections, &[],
        );

        // .text is first (from script), then remainder sorted by default.
        assert_eq!(output_sections[0].name, ".text");
        assert!(output_sections.len() == 4);
        // Check section map has all 4 sections
        assert_eq!(section_map.len(), 4);
    }

    #[test]
    fn script_merge_with_fixed_address() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        let dead = HashSet::new();

        let script_sections = vec![
            (".text".to_string(), vec!["*(.text)".to_string()], Some(0x400000)),
        ];

        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &script_sections, &[],
        );

        assert_eq!(output_sections[0].addr, 0x400000);
    }

    #[test]
    fn script_merge_keep_overrides_gc() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
            (".init_array", SHT_PROGBITS, SHF_WRITE, 8, 8),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        // Mark .init_array as dead
        let mut dead = HashSet::new();
        dead.insert((0usize, 1usize));

        let script_sections = vec![
            (".text".to_string(), vec!["*(.text)".to_string()], None),
            (".init_array".to_string(), vec!["*(.init_array)".to_string()], None),
        ];
        let keep_patterns = vec!["*(.init_array)".to_string()];

        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &script_sections, &keep_patterns,
        );

        // .init_array should be present despite being in dead_sections
        // because KEEP() overrides GC.
        assert_eq!(output_sections.len(), 2);
        assert_eq!(output_sections[1].name, ".init_array");
        assert_eq!(output_sections[1].inputs.len(), 1);
    }

    #[test]
    fn script_merge_wildcard_patterns() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
            (".text.hot", SHT_PROGBITS, SHF_EXECINSTR, 50, 16),
            (".text.cold", SHT_PROGBITS, SHF_EXECINSTR, 30, 16),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        let dead = HashSet::new();

        // Script uses wildcard to gather all .text* sections
        let script_sections = vec![
            (".text".to_string(), vec![
                "*(.text)".to_string(),
                "*(.text.*)".to_string(),
            ], None),
        ];

        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &script_sections, &[],
        );

        // All three .text* sections should be in one output section.
        assert_eq!(output_sections.len(), 1);
        assert_eq!(output_sections[0].name, ".text");
        assert_eq!(output_sections[0].inputs.len(), 3);
    }

    #[test]
    fn script_merge_empty_script_falls_through() {
        let obj = make_object(vec![
            (".text", SHT_PROGBITS, SHF_EXECINSTR, 100, 16),
            (".rodata", SHT_PROGBITS, 0, 30, 4),
        ]);
        let objects = vec![obj];
        let mut output_sections = Vec::new();
        let mut section_map = HashMap::new();
        let dead = HashSet::new();

        // Empty script — everything falls through to default.
        merge_sections_with_script::<DummyGlobal>(
            &objects, &mut output_sections, &mut section_map,
            &dead, &[], &[],
        );

        // Sections should be sorted by default: RO (.rodata) → Exec (.text)
        assert_eq!(output_sections.len(), 2);
        assert_eq!(output_sections[0].name, ".rodata");
        assert_eq!(output_sections[1].name, ".text");
    }

    // Dummy GlobalSymbolOps implementation for testing.
    #[derive(Clone)]
    struct DummyGlobal;
    impl GlobalSymbolOps for DummyGlobal {
        fn is_defined(&self) -> bool { false }
        fn is_dynamic(&self) -> bool { false }
        fn info(&self) -> u8 { 0 }
        fn section_idx(&self) -> u16 { 0 }
        fn value(&self) -> u64 { 0 }
        fn size(&self) -> u64 { 0 }
        fn new_defined(_: usize, _: &crate::backend::linker_common::types::Elf64Symbol) -> Self { DummyGlobal }
        fn new_common(_: usize, _: &crate::backend::linker_common::types::Elf64Symbol) -> Self { DummyGlobal }
        fn new_undefined(_: &crate::backend::linker_common::types::Elf64Symbol) -> Self { DummyGlobal }
        fn set_common_bss(&mut self, _: u64) {}
        fn new_dynamic(_: &crate::backend::linker_common::types::DynSymbol, _: &str) -> Self { DummyGlobal }
    }
}
