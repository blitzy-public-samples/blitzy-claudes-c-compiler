//! Section merging for the i686 linker.
//!
//! Phase 5 of the linking pipeline: merges input sections from all objects
//! into output sections, handling COMDAT group deduplication and section
//! type/flag assignment.
//!
//! Supports optional linker script `SECTIONS { }` and `KEEP(...)` directives
//! for script-driven section placement via [`merge_sections_with_script`].

use std::collections::{HashMap, HashSet};

use super::types::*;
use crate::backend::linker_common::linker_script::{LinkerScript, ScriptSection, InputPattern};

pub(super) fn merge_sections(
    inputs: &[InputObject],
) -> (Vec<OutputSection>, HashMap<String, usize>, SectionMap) {
    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_name_to_idx: HashMap<String, usize> = HashMap::new();
    let mut section_map: SectionMap = HashMap::new();
    let mut included_comdat_sections: HashSet<String> = HashSet::new();

    // COMDAT group deduplication
    let comdat_skip = compute_comdat_skip(inputs);

    for (obj_idx, obj) in inputs.iter().enumerate() {
        for sec in obj.sections.iter() {
            if comdat_skip.contains(&(obj_idx, sec.input_index)) {
                continue;
            }
            let out_name = match output_section_name(&sec.name, sec.flags, sec.sh_type) {
                Some(n) => n,
                None => continue,
            };

            // COMDAT deduplication by section name
            if sec.flags & SHF_GROUP != 0 && !included_comdat_sections.insert(sec.name.clone()) {
                continue;
            }

            let out_idx = if let Some(&idx) = section_name_to_idx.get(&out_name) {
                idx
            } else {
                let idx = output_sections.len();
                let (sh_type, flags) = section_type_and_flags(&out_name, sec);
                section_name_to_idx.insert(out_name.clone(), idx);
                output_sections.push(OutputSection {
                    name: out_name,
                    sh_type,
                    flags,
                    data: Vec::new(),
                    align: 1,
                    addr: 0,
                    file_offset: 0,
                });
                idx
            };

            let out_sec = &mut output_sections[out_idx];
            // .init and .fini must be concatenated without padding
            let align = if out_sec.name == ".init" || out_sec.name == ".fini" {
                1
            } else {
                sec.align.max(1)
            };
            if align > out_sec.align {
                out_sec.align = align;
            }
            let padding = (align - (out_sec.data.len() as u32 % align)) % align;
            out_sec.data.extend(std::iter::repeat_n(0u8, padding as usize));
            let offset = out_sec.data.len() as u32;

            section_map.insert((obj_idx, sec.input_index), (out_idx, offset));

            if sec.sh_type != SHT_NOBITS {
                out_sec.data.extend_from_slice(&sec.data);
            } else {
                out_sec.data.extend(std::iter::repeat_n(0u8, sec.data.len()));
            }
        }
    }

    (output_sections, section_name_to_idx, section_map)
}

/// Merge sections with optional linker script SECTIONS directives.
///
/// When `script` is `None` or contains no SECTIONS directives, delegates
/// entirely to [`merge_sections`] for backward compatibility — the output
/// is identical to calling `merge_sections(inputs)` directly.
///
/// When a linker script is provided with SECTIONS directives:
///
/// 1. Script-specified output sections are created first, in script order.
///    Each output section inherits an optional fixed address from the script's
///    [`ScriptSection::address`] field (truncated to `u32` for ELF32).
/// 2. Input sections whose names match the script's wildcard patterns
///    ([`InputPattern::section_patterns`]) are placed into the corresponding
///    output section. File-level patterns ([`InputPattern::file_pattern`])
///    are matched against the input object's filename.
/// 3. Remaining input sections not matched by any script rule are placed
///    using the default merge logic (same canonicalization and ordering as
///    [`merge_sections`]).
/// 4. `KEEP` patterns from [`LinkerScript::keep_patterns`],
///    [`ScriptSection::keep`], and [`InputPattern::keep`] are tracked for
///    future `--gc-sections` support.  Currently `--gc-sections` is not
///    implemented in the i686 linker, so KEEP is preparatory infrastructure.
///
/// COMDAT deduplication via [`compute_comdat_skip`] is applied identically
/// regardless of whether a linker script is present.
pub(super) fn merge_sections_with_script(
    inputs: &[InputObject],
    script: Option<&LinkerScript>,
) -> (Vec<OutputSection>, HashMap<String, usize>, SectionMap) {
    // When no linker script is provided or it has no SECTIONS directives,
    // delegate to the default merge logic for full backward compatibility.
    let script = match script {
        Some(s) if !s.sections.is_empty() => s,
        _ => return merge_sections(inputs),
    };

    let mut output_sections: Vec<OutputSection> = Vec::new();
    let mut section_name_to_idx: HashMap<String, usize> = HashMap::new();
    let mut section_map: SectionMap = HashMap::new();
    let mut included_comdat_sections: HashSet<String> = HashSet::new();
    let mut placed: HashSet<(usize, usize)> = HashSet::new();

    // COMDAT group deduplication — identical to merge_sections.
    let comdat_skip = compute_comdat_skip(inputs);

    // KEEP tracking: accumulate output section indices that are marked as
    // retained.  When --gc-sections is eventually implemented, these sections
    // will bypass reachability analysis and remain in the output.
    let keep_patterns: &[String] = &script.keep_patterns;
    let mut _kept_output_indices: HashSet<usize> = HashSet::new();

    // ── Pass 1: Create output sections from linker script in order ──────
    //
    // Each ScriptSection in the linker script's SECTIONS block defines an
    // output section.  Input sections matching its patterns are placed into
    // it.  The order of ScriptSections determines the output section order.
    for script_sec in &script.sections {
        let out_idx = output_sections.len();
        section_name_to_idx.insert(script_sec.name.clone(), out_idx);

        // Use address from script, truncated to u32 for ELF32.
        let addr = script_sec.address.map(|a| a as u32).unwrap_or(0);

        output_sections.push(OutputSection {
            name: script_sec.name.clone(),
            sh_type: SHT_PROGBITS,
            flags: infer_section_flags(&script_sec.name),
            data: Vec::new(),
            align: 1,
            addr,
            file_offset: 0,
        });

        // Accumulate KEEP retention from all three sources:
        //  1. ScriptSection.keep — the entire output section is KEEP'd
        //  2. InputPattern.keep  — individual input patterns within the section
        //  3. Global keep_patterns — top-level KEEP() directives from the script
        if script_sec.keep
            || script_sec.input_patterns.iter().any(|ip: &InputPattern| ip.keep)
            || keep_patterns.iter().any(|kp| match_wildcard(kp, &script_sec.name))
        {
            _kept_output_indices.insert(out_idx);
        }

        // Match input sections to this script section's wildcard patterns.
        for (obj_idx, obj) in inputs.iter().enumerate() {
            for sec in obj.sections.iter() {
                let key = (obj_idx, sec.input_index);
                if comdat_skip.contains(&key) {
                    continue;
                }
                if placed.contains(&key) {
                    continue;
                }

                // Skip metadata sections that should not appear in output
                // (symbol tables, relocation sections, etc.).
                if !is_mergeable_section(sec) {
                    continue;
                }

                // Check if section matches any InputPattern for this script entry.
                if !matches_script_patterns(&sec.name, &obj.filename, script_sec) {
                    continue;
                }

                // COMDAT deduplication by section name (same as merge_sections).
                if sec.flags & SHF_GROUP != 0
                    && !included_comdat_sections.insert(sec.name.clone())
                {
                    continue;
                }

                placed.insert(key);

                let out_sec = &mut output_sections[out_idx];
                // .init and .fini must be concatenated without padding
                let align = if out_sec.name == ".init" || out_sec.name == ".fini" {
                    1
                } else {
                    sec.align.max(1)
                };
                if align > out_sec.align {
                    out_sec.align = align;
                }
                let padding = (align - (out_sec.data.len() as u32 % align)) % align;
                out_sec.data.extend(std::iter::repeat_n(0u8, padding as usize));
                let offset = out_sec.data.len() as u32;

                section_map.insert(key, (out_idx, offset));

                // Refine output section type and flags from actual input sections.
                out_sec.flags |= sec.flags & (SHF_WRITE | SHF_EXECINSTR | SHF_ALLOC | SHF_TLS);
                if sec.sh_type == SHT_PROGBITS {
                    out_sec.sh_type = SHT_PROGBITS;
                } else if sec.sh_type == SHT_NOBITS && out_sec.sh_type != SHT_PROGBITS {
                    out_sec.sh_type = SHT_NOBITS;
                }

                if sec.sh_type != SHT_NOBITS {
                    out_sec.data.extend_from_slice(&sec.data);
                } else {
                    out_sec.data.extend(std::iter::repeat_n(0u8, sec.data.len()));
                }
            }
        }
    }

    // ── Pass 2: Merge remaining sections using default logic ────────────
    //
    // Sections not matched by any linker script rule fall through to the
    // standard merge_sections canonicalization and ordering.
    for (obj_idx, obj) in inputs.iter().enumerate() {
        for sec in obj.sections.iter() {
            let key = (obj_idx, sec.input_index);
            if comdat_skip.contains(&key) {
                continue;
            }
            if placed.contains(&key) {
                continue;
            }

            let out_name = match output_section_name(&sec.name, sec.flags, sec.sh_type) {
                Some(n) => n,
                None => continue,
            };

            // COMDAT deduplication by section name
            if sec.flags & SHF_GROUP != 0
                && !included_comdat_sections.insert(sec.name.clone())
            {
                continue;
            }

            let out_idx = if let Some(&idx) = section_name_to_idx.get(&out_name) {
                idx
            } else {
                let idx = output_sections.len();
                let (sh_type, flags) = section_type_and_flags(&out_name, sec);
                section_name_to_idx.insert(out_name.clone(), idx);
                output_sections.push(OutputSection {
                    name: out_name,
                    sh_type,
                    flags,
                    data: Vec::new(),
                    align: 1,
                    addr: 0,
                    file_offset: 0,
                });
                idx
            };

            let out_sec = &mut output_sections[out_idx];
            // .init and .fini must be concatenated without padding
            let align = if out_sec.name == ".init" || out_sec.name == ".fini" {
                1
            } else {
                sec.align.max(1)
            };
            if align > out_sec.align {
                out_sec.align = align;
            }
            let padding = (align - (out_sec.data.len() as u32 % align)) % align;
            out_sec.data.extend(std::iter::repeat_n(0u8, padding as usize));
            let offset = out_sec.data.len() as u32;

            section_map.insert(key, (out_idx, offset));

            if sec.sh_type != SHT_NOBITS {
                out_sec.data.extend_from_slice(&sec.data);
            } else {
                out_sec.data.extend(std::iter::repeat_n(0u8, sec.data.len()));
            }
        }
    }

    (output_sections, section_name_to_idx, section_map)
}

// ══════════════════════════════════════════════════════════════════════════════
// Linker script wildcard matching and section pattern helpers
// ══════════════════════════════════════════════════════════════════════════════

/// Check whether an input section is eligible for merging into an output
/// section.  Filters out metadata sections (symbol tables, relocation
/// sections, group sections, etc.) that should never appear in the final
/// output regardless of linker script directives.
fn is_mergeable_section(sec: &InputSection) -> bool {
    // Skip symbol tables, string tables, relocation sections, and group
    // sections — these are consumed during earlier linker phases and are
    // not emitted as loadable output sections.
    if sec.sh_type == SHT_NULL
        || sec.sh_type == SHT_SYMTAB
        || sec.sh_type == SHT_STRTAB
        || sec.sh_type == SHT_REL
        || sec.sh_type == SHT_RELA
        || sec.sh_type == SHT_GROUP
    {
        return false;
    }
    // Skip well-known non-output sections that are never placed in the
    // final executable (stack notes, comment strings).
    if sec.name == ".note.GNU-stack" || sec.name == ".comment" {
        return false;
    }
    true
}

/// Check whether an input section name matches any pattern in a linker
/// script [`ScriptSection`] definition.
///
/// Iterates over the script section's [`ScriptSection::input_patterns`],
/// checking the file-level pattern ([`InputPattern::file_pattern`]) against
/// the object filename and each section name wildcard
/// ([`InputPattern::section_patterns`]) against the input section name.
fn matches_script_patterns(
    sec_name: &str,
    obj_filename: &str,
    script_sec: &ScriptSection,
) -> bool {
    for pat in &script_sec.input_patterns {
        // Check file pattern.  `*` and empty string match all files;
        // otherwise perform wildcard matching against the object filename.
        let file_matches = pat.file_pattern.is_empty()
            || pat.file_pattern == "*"
            || match_wildcard(&pat.file_pattern, obj_filename);
        if !file_matches {
            continue;
        }

        // Check each section name pattern for a wildcard match.
        for sec_pat in &pat.section_patterns {
            if match_wildcard(sec_pat, sec_name) {
                return true;
            }
        }
    }
    false
}

/// Match a string against a wildcard pattern.
///
/// Supports two wildcard characters:
/// - `*` matches any sequence of characters (including the empty string).
/// - `?` matches exactly one character.
///
/// All other characters are compared literally.  The algorithm uses
/// backtracking from the most recent `*` to handle patterns like
/// `.text.*` or `*(.rodata.*.cst?)`.
fn match_wildcard(pattern: &str, name: &str) -> bool {
    let p = pattern.as_bytes();
    let n = name.as_bytes();
    let plen = p.len();
    let nlen = n.len();
    let mut pi = 0usize;
    let mut ni = 0usize;
    // Track the most recent `*` position for backtracking.
    let mut star_pi = usize::MAX;
    let mut star_ni = 0usize;

    while ni < nlen {
        if pi < plen && (p[pi] == b'?' || p[pi] == n[ni]) {
            // Exact character match or single-character wildcard.
            pi += 1;
            ni += 1;
        } else if pi < plen && p[pi] == b'*' {
            // Record the star position; initially match zero characters.
            star_pi = pi;
            star_ni = ni;
            pi += 1;
        } else if star_pi != usize::MAX {
            // Mismatch — backtrack to the last `*` and consume one more
            // character from the name.
            pi = star_pi + 1;
            star_ni += 1;
            ni = star_ni;
        } else {
            return false;
        }
    }

    // Consume any trailing `*` characters in the pattern.
    while pi < plen && p[pi] == b'*' {
        pi += 1;
    }

    pi == plen
}

/// Infer default ELF section flags from a well-known output section name.
///
/// Used when creating output sections from linker script SECTIONS directives
/// before any input sections have been merged (flags are refined as input
/// sections are accumulated).
fn infer_section_flags(name: &str) -> u32 {
    match name {
        ".text" | ".init" | ".fini" | ".plt" => SHF_ALLOC | SHF_EXECINSTR,
        ".rodata" | ".eh_frame" | ".note" => SHF_ALLOC,
        ".data" | ".init_array" | ".fini_array" | ".got" | ".got.plt" => SHF_ALLOC | SHF_WRITE,
        ".bss" => SHF_ALLOC | SHF_WRITE,
        ".tdata" => SHF_ALLOC | SHF_WRITE | SHF_TLS,
        ".tbss" => SHF_ALLOC | SHF_WRITE | SHF_TLS,
        _ => SHF_ALLOC,
    }
}

pub(super) fn compute_comdat_skip(inputs: &[InputObject]) -> HashSet<(usize, usize)> {
    let mut comdat_skip = HashSet::new();
    let mut seen_groups: HashSet<String> = HashSet::new();

    for (obj_idx, obj) in inputs.iter().enumerate() {
        for sec in obj.sections.iter() {
            if sec.sh_type != SHT_GROUP { continue; }
            if sec.data.len() < 4 { continue; }
            let flags = read_u32(&sec.data, 0);
            if flags & 1 == 0 { continue; }
            let sig_name = if (sec.info as usize) < obj.symbols.len() {
                obj.symbols[sec.info as usize].name.clone()
            } else {
                continue;
            };
            if !seen_groups.insert(sig_name) {
                let mut off = 4;
                while off + 4 <= sec.data.len() {
                    let member_idx = read_u32(&sec.data, off) as usize;
                    comdat_skip.insert((obj_idx, member_idx));
                    off += 4;
                }
            }
        }
    }

    comdat_skip
}

pub(super) fn section_type_and_flags(out_name: &str, sec: &InputSection) -> (u32, u32) {
    match out_name {
        ".text" => (SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR),
        ".rodata" => (SHT_PROGBITS, SHF_ALLOC),
        ".data" => (SHT_PROGBITS, SHF_ALLOC | SHF_WRITE),
        ".bss" => (SHT_NOBITS, SHF_ALLOC | SHF_WRITE),
        ".tdata" => (SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS),
        ".tbss" => (SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS),
        ".init" | ".fini" => (SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR),
        ".init_array" => (SHT_INIT_ARRAY, SHF_ALLOC | SHF_WRITE),
        ".fini_array" => (SHT_FINI_ARRAY, SHF_ALLOC | SHF_WRITE),
        ".eh_frame" => (SHT_PROGBITS, SHF_ALLOC),
        ".note" => (SHT_NOTE, SHF_ALLOC),
        _ => (sec.sh_type, sec.flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR)),
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Phase 6: Symbol resolution
// ══════════════════════════════════════════════════════════════════════════════

