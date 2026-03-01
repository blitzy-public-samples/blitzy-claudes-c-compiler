//! Pragma directive handling.
//!
//! Handles #pragma once, pack, push_macro/pop_macro, weak,
//! redefine_extname, and GCC visibility directives.

use super::pipeline::Preprocessor;

impl Preprocessor {
    pub(super) fn handle_pragma(&mut self, rest: &str) -> Option<String> {
        let rest = rest.trim();
        if rest == "once" {
            // Mark the current file as "include once" using device+inode for
            // reliable deduplication across symlinks and hard links (C11 §6.10.6).
            if let Some(current_file) = self.include_stack.last() {
                use std::os::unix::fs::MetadataExt;
                if let Ok(metadata) = std::fs::metadata(current_file) {
                    self.pragma_once_inodes.insert((metadata.dev(), metadata.ino()));
                } else {
                    // Fallback to path-based tracking when metadata is unavailable
                    // (e.g., virtual filesystems or permission errors).
                    self.pragma_once_files.insert(current_file.clone());
                }
            }
            return None;
        }

        // Handle #pragma pack directives (suppress in asm mode since these
        // emit synthetic __ccc_pack_* tokens that the assembler can't parse)
        if let Some(pack_content) = rest.strip_prefix("pack") {
            if self.macros.asm_mode {
                return None;
            }
            return self.handle_pragma_pack(pack_content.trim());
        }

        // Handle #pragma push_macro("name") / pop_macro("name")
        if let Some(push_content) = rest.strip_prefix("push_macro") {
            self.handle_pragma_push_macro(push_content.trim());
            return None;
        }
        if let Some(pop_content) = rest.strip_prefix("pop_macro") {
            self.handle_pragma_pop_macro(pop_content.trim());
            return None;
        }

        // Handle #pragma weak symbol [= alias]
        if let Some(weak_content) = rest.strip_prefix("weak") {
            self.handle_pragma_weak(weak_content.trim());
            return None;
        }

        // Handle #pragma redefine_extname old new
        if let Some(redefine_content) = rest.strip_prefix("redefine_extname") {
            self.handle_pragma_redefine_extname(redefine_content.trim());
            return None;
        }

        // Handle #pragma GCC visibility push(hidden|default|protected|internal) / pop
        // Suppressed in asm mode: synthetic __ccc_visibility_* tokens are C parser-
        // specific and would cause assembler errors when preprocessing .S files.
        if let Some(gcc_content) = rest.strip_prefix("GCC") {
            let gcc_content = gcc_content.trim();
            if let Some(vis_content) = gcc_content.strip_prefix("visibility") {
                if self.macros.asm_mode {
                    return None;
                }
                return self.handle_pragma_gcc_visibility(vis_content.trim());
            }
        }

        // Other pragmas (GCC, diagnostic, etc.) are silently ignored
        None
    }

    /// Handle #pragma GCC visibility push(hidden|default|protected|internal) / pop.
    /// Emits synthetic tokens for the parser to track default visibility.
    fn handle_pragma_gcc_visibility(&mut self, content: &str) -> Option<String> {
        let content = content.trim();
        if content == "pop" {
            return Some("__ccc_visibility_pop ;\n".to_string());
        }
        if let Some(rest) = content.strip_prefix("push") {
            let rest = rest.trim();
            if rest.starts_with('(') {
                let inner = rest.trim_start_matches('(').trim_end_matches(')').trim();
                match inner {
                    "hidden" | "default" | "protected" | "internal" => {
                        return Some(format!("__ccc_visibility_push_{} ;\n", inner));
                    }
                    _ => {}
                }
            }
        }
        None
    }

    /// Handle #pragma push_macro("name") - save the current definition of macro.
    fn handle_pragma_push_macro(&mut self, content: &str) {
        if let Some(name) = Self::extract_pragma_macro_name(content) {
            let saved = self.macros.get(&name).cloned();
            self.macro_save_stack
                .entry(name)
                .or_default()
                .push(saved);
        }
    }

    /// Handle #pragma pop_macro("name") - restore the previously saved definition.
    fn handle_pragma_pop_macro(&mut self, content: &str) {
        if let Some(name) = Self::extract_pragma_macro_name(content) {
            if let Some(stack) = self.macro_save_stack.get_mut(&name) {
                if let Some(saved) = stack.pop() {
                    match saved {
                        Some(def) => self.macros.define(def),
                        None => self.macros.undefine(&name),
                    }
                }
            }
        }
    }

    /// Extract macro name from pragma argument like ("name").
    fn extract_pragma_macro_name(content: &str) -> Option<String> {
        let content = content.trim();
        if !content.starts_with('(') {
            return None;
        }
        let inner = content.trim_start_matches('(').trim_end_matches(')').trim();
        // Strip quotes
        let name = inner.trim_matches('"');
        if name.is_empty() {
            return None;
        }
        Some(name.to_string())
    }

    /// Handle #pragma weak directives.
    /// Forms:
    ///   #pragma weak symbol         - mark symbol as weak
    ///   #pragma weak symbol = target - symbol becomes a weak alias for target
    fn handle_pragma_weak(&mut self, content: &str) {
        let content = content.trim();
        if content.is_empty() {
            return;
        }
        if let Some(eq_pos) = content.find('=') {
            let symbol = content[..eq_pos].trim().to_string();
            let target = content[eq_pos + 1..].trim().to_string();
            if !symbol.is_empty() && !target.is_empty() {
                self.weak_pragmas.push((symbol, Some(target)));
            }
        } else {
            // Just mark the symbol as weak
            let symbol = content.split_whitespace().next().unwrap_or("").to_string();
            if !symbol.is_empty() {
                self.weak_pragmas.push((symbol, None));
            }
        }
    }

    /// Handle #pragma redefine_extname old new
    /// Redirects external symbol 'old' to 'new' (non-weak alias).
    fn handle_pragma_redefine_extname(&mut self, content: &str) {
        let parts: Vec<&str> = content.split_whitespace().collect();
        if parts.len() >= 2 {
            let old_name = parts[0].to_string();
            let new_name = parts[1].to_string();
            // Redirect external references from old_name to new_name.
            self.redefine_extname_pragmas.push((old_name, new_name));
        }
    }

    /// Handle #pragma pack directives and emit synthetic tokens for the parser.
    ///
    /// Also maintains the preprocessor-side pack alignment stack for _Pragma
    /// desugaring coordination and future preprocessor-level queries.
    ///
    /// Supported forms:
    ///   #pragma pack(N)                   - set alignment to N
    ///   #pragma pack()                    - reset to default alignment
    ///   #pragma pack(push, N)             - push current and set to N
    ///   #pragma pack(push)                - push current (no change)
    ///   #pragma pack(push, identifier, N) - GCC extension: push with named id, set to N
    ///   #pragma pack(pop)                 - restore previous alignment
    ///   #pragma pack(pop, identifier)     - GCC extension: pop with named id
    fn handle_pragma_pack(&mut self, content: &str) -> Option<String> {
        let content = content.trim();
        // Must start with '('
        if !content.starts_with('(') {
            return None;
        }
        let inner = content.trim_start_matches('(').trim_end_matches(')').trim();

        if inner.is_empty() {
            // #pragma pack() - reset to default alignment
            self.current_pack_alignment = None;
            return Some("__ccc_pack_reset ;\n".to_string());
        }

        // Check for pop (with optional named identifier — GCC/MSVC extension).
        // The identifier after pop is used by MSVC for targeted named pop;
        // GCC and CCC simply pop the top of the alignment stack regardless.
        if inner.starts_with("pop") {
            let after_pop = inner["pop".len()..].trim();
            if after_pop.is_empty() || after_pop.starts_with(',') {
                // #pragma pack(pop) or #pragma pack(pop, identifier)
                self.current_pack_alignment = self.pack_alignment_stack.pop().unwrap_or(None);
                return Some("__ccc_pack_pop ;\n".to_string());
            }
        }

        if let Some(rest) = inner.strip_prefix("push") {
            let rest = rest.trim().trim_start_matches(',').trim();
            if rest.is_empty() {
                // #pragma pack(push) - push current alignment, don't change
                self.pack_alignment_stack.push(self.current_pack_alignment);
                return Some("__ccc_pack_push_only ;\n".to_string());
            }
            // #pragma pack(push, N) - push current and set to N (0 means default)
            if let Ok(n) = rest.parse::<usize>() {
                self.pack_alignment_stack.push(self.current_pack_alignment);
                self.current_pack_alignment = if n == 0 { None } else { Some(n) };
                return Some(format!("__ccc_pack_push_{} ;\n", n));
            }
            // GCC extension: #pragma pack(push, identifier, N) — push with named
            // identifier. The identifier is used by MSVC for named pop targeting;
            // GCC/CCC ignores the identifier and treats this as push + set N.
            if let Some(comma_pos) = rest.find(',') {
                let after_comma = rest[comma_pos + 1..].trim();
                if let Ok(n) = after_comma.parse::<usize>() {
                    self.pack_alignment_stack.push(self.current_pack_alignment);
                    self.current_pack_alignment = if n == 0 { None } else { Some(n) };
                    return Some(format!("__ccc_pack_push_{} ;\n", n));
                }
            }
            return None;
        }

        // #pragma pack(N) - set alignment (0 means reset to default)
        if let Ok(n) = inner.parse::<usize>() {
            self.current_pack_alignment = if n == 0 { None } else { Some(n) };
            return Some(format!("__ccc_pack_set_{} ;\n", n));
        }

        None
    }
}

#[cfg(test)]
mod tests {
    use crate::frontend::preprocessor::pipeline::Preprocessor;

    /// Helper: call handle_pragma on a fresh or provided Preprocessor.
    fn pragma(pp: &mut Preprocessor, rest: &str) -> Option<String> {
        pp.handle_pragma(rest)
    }

    // ---- Existing #pragma pack forms (regression) ----

    #[test]
    fn test_pack_set_n() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "pack(4)"), Some("__ccc_pack_set_4 ;\n".to_string()));
        assert_eq!(pragma(&mut pp, "pack(1)"), Some("__ccc_pack_set_1 ;\n".to_string()));
        assert_eq!(pragma(&mut pp, "pack(16)"), Some("__ccc_pack_set_16 ;\n".to_string()));
    }

    #[test]
    fn test_pack_reset() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "pack()"), Some("__ccc_pack_reset ;\n".to_string()));
    }

    #[test]
    fn test_pack_push_only() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "pack(push)"), Some("__ccc_pack_push_only ;\n".to_string()));
    }

    #[test]
    fn test_pack_push_n() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "pack(push, 2)"), Some("__ccc_pack_push_2 ;\n".to_string()));
        assert_eq!(pragma(&mut pp, "pack(push, 8)"), Some("__ccc_pack_push_8 ;\n".to_string()));
    }

    #[test]
    fn test_pack_pop() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "pack(pop)"), Some("__ccc_pack_pop ;\n".to_string()));
    }

    // ---- New GCC extension forms ----

    #[test]
    fn test_pack_push_identifier_n() {
        let mut pp = Preprocessor::new();
        // #pragma pack(push, myid, 1) should emit __ccc_pack_push_1
        assert_eq!(pragma(&mut pp, "pack(push, myid, 1)"), Some("__ccc_pack_push_1 ;\n".to_string()));
        // Different identifier and value
        assert_eq!(pragma(&mut pp, "pack(push, another_id, 8)"), Some("__ccc_pack_push_8 ;\n".to_string()));
    }

    #[test]
    fn test_pack_pop_identifier() {
        let mut pp = Preprocessor::new();
        // #pragma pack(pop, myid) should emit __ccc_pack_pop
        assert_eq!(pragma(&mut pp, "pack(pop, myid)"), Some("__ccc_pack_pop ;\n".to_string()));
        assert_eq!(pragma(&mut pp, "pack(pop, other)"), Some("__ccc_pack_pop ;\n".to_string()));
    }

    // ---- Pack alignment stack tracking ----

    #[test]
    fn test_pack_stack_set_tracks_alignment() {
        let mut pp = Preprocessor::new();
        assert_eq!(pp.current_pack_alignment, None);
        pragma(&mut pp, "pack(4)");
        assert_eq!(pp.current_pack_alignment, Some(4));
        assert!(pp.pack_alignment_stack.is_empty(), "set does not push to stack");
    }

    #[test]
    fn test_pack_stack_reset_clears_alignment() {
        let mut pp = Preprocessor::new();
        pragma(&mut pp, "pack(4)");
        assert_eq!(pp.current_pack_alignment, Some(4));
        pragma(&mut pp, "pack()");
        assert_eq!(pp.current_pack_alignment, None);
    }

    #[test]
    fn test_pack_stack_push_pop_cycle() {
        let mut pp = Preprocessor::new();

        // Set initial alignment to 4
        pragma(&mut pp, "pack(4)");
        assert_eq!(pp.current_pack_alignment, Some(4));

        // Push current (4), set to 2
        pragma(&mut pp, "pack(push, 2)");
        assert_eq!(pp.current_pack_alignment, Some(2));
        assert_eq!(pp.pack_alignment_stack.len(), 1);
        assert_eq!(pp.pack_alignment_stack[0], Some(4));

        // Pop: restore to 4
        pragma(&mut pp, "pack(pop)");
        assert_eq!(pp.current_pack_alignment, Some(4));
        assert!(pp.pack_alignment_stack.is_empty());

        // Pop on empty stack: reset to None (default)
        pragma(&mut pp, "pack(pop)");
        assert_eq!(pp.current_pack_alignment, None);
    }

    #[test]
    fn test_pack_stack_push_only_preserves_current() {
        let mut pp = Preprocessor::new();
        pragma(&mut pp, "pack(8)");
        pragma(&mut pp, "pack(push)");
        // push-only should NOT change current alignment
        assert_eq!(pp.current_pack_alignment, Some(8));
        assert_eq!(pp.pack_alignment_stack.len(), 1);
        assert_eq!(pp.pack_alignment_stack[0], Some(8));
    }

    #[test]
    fn test_pack_stack_push_identifier_n_tracks() {
        let mut pp = Preprocessor::new();
        pragma(&mut pp, "pack(4)");
        pragma(&mut pp, "pack(push, myname, 1)");
        assert_eq!(pp.current_pack_alignment, Some(1));
        assert_eq!(pp.pack_alignment_stack.len(), 1);
        assert_eq!(pp.pack_alignment_stack[0], Some(4));
    }

    #[test]
    fn test_pack_stack_pop_identifier_tracks() {
        let mut pp = Preprocessor::new();
        pragma(&mut pp, "pack(push, 2)");
        assert_eq!(pp.current_pack_alignment, Some(2));
        pragma(&mut pp, "pack(pop, myname)");
        assert_eq!(pp.current_pack_alignment, None); // restored from stack (None was default)
        assert!(pp.pack_alignment_stack.is_empty());
    }

    #[test]
    fn test_pack_zero_means_default() {
        let mut pp = Preprocessor::new();
        pragma(&mut pp, "pack(4)");
        pragma(&mut pp, "pack(0)");
        assert_eq!(pp.current_pack_alignment, None, "pack(0) means default");

        pragma(&mut pp, "pack(push, 0)");
        assert_eq!(pp.current_pack_alignment, None, "push,0 means push and default");
        assert_eq!(pp.pack_alignment_stack.len(), 1);
    }

    #[test]
    fn test_pack_nested_push_pop_sequence() {
        let mut pp = Preprocessor::new();

        // Push level 1
        pragma(&mut pp, "pack(push, 1)");
        assert_eq!(pp.current_pack_alignment, Some(1));

        // Push level 2 with identifier
        pragma(&mut pp, "pack(push, id2, 2)");
        assert_eq!(pp.current_pack_alignment, Some(2));
        assert_eq!(pp.pack_alignment_stack.len(), 2);

        // Set without push
        pragma(&mut pp, "pack(4)");
        assert_eq!(pp.current_pack_alignment, Some(4));
        assert_eq!(pp.pack_alignment_stack.len(), 2, "set doesn't affect stack");

        // Pop with identifier: restores to level 1's alignment (1)
        pragma(&mut pp, "pack(pop, id2)");
        assert_eq!(pp.current_pack_alignment, Some(1));
        assert_eq!(pp.pack_alignment_stack.len(), 1);

        // Pop: restores to initial None
        pragma(&mut pp, "pack(pop)");
        assert_eq!(pp.current_pack_alignment, None);
        assert!(pp.pack_alignment_stack.is_empty());
    }

    // ---- Other pragma handlers unchanged ----

    #[test]
    fn test_pragma_once_returns_none() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "once"), None);
    }

    #[test]
    fn test_pragma_weak_returns_none() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "weak mysym"), None);
    }

    #[test]
    fn test_pragma_unknown_returns_none() {
        let mut pp = Preprocessor::new();
        assert_eq!(pragma(&mut pp, "something_unknown"), None);
    }

    #[test]
    fn test_pragma_gcc_visibility() {
        let mut pp = Preprocessor::new();
        assert_eq!(
            pragma(&mut pp, "GCC visibility push(hidden)"),
            Some("__ccc_visibility_push_hidden ;\n".to_string())
        );
        assert_eq!(
            pragma(&mut pp, "GCC visibility pop"),
            Some("__ccc_visibility_pop ;\n".to_string())
        );
    }

    #[test]
    fn test_pack_invalid_returns_none() {
        let mut pp = Preprocessor::new();
        // No parentheses
        assert_eq!(pragma(&mut pp, "pack"), None);
        // Invalid content
        assert_eq!(pragma(&mut pp, "pack(push, abc)"), None);
        // Invalid number in push, id, N form
        assert_eq!(pragma(&mut pp, "pack(push, id, abc)"), None);
    }
}
