//! GNU ld-compatible linker script parser.
//!
//! Parses linker scripts specified via `-T script.ld` for section placement,
//! memory layout, entry point configuration, and symbol generation.
//!
//! Supports the following directives:
//! - `SECTIONS { ... }` — output section definitions with input section wildcards
//! - `MEMORY { ... }` — memory region definitions
//! - `ENTRY(symbol)` — entry point specification
//! - `PROVIDE(symbol = expr)` — conditional symbol definition
//! - `PROVIDE_HIDDEN(symbol = expr)` — conditional hidden symbol definition
//! - `KEEP(...)` — section retention for garbage collection
//!
//! The parser produces a `LinkerScript` struct consumed by per-architecture
//! linkers in `src/backend/x86/linker/`, `src/backend/arm/linker/`, etc.
//!
//! This complements the existing `GROUP`/`INPUT` directive parser in
//! `src/backend/elf/archive.rs`, which handles shared library resolution.

use std::path::Path;

// ---------------------------------------------------------------------------
// Public data structures
// ---------------------------------------------------------------------------

/// A parsed GNU ld-compatible linker script.
///
/// Contains all directives needed for section placement, memory layout,
/// symbol generation, and entry point configuration. Produced by
/// `parse_linker_script()` and consumed by per-architecture linkers.
#[derive(Debug, Default)]
pub struct LinkerScript {
    /// Entry point symbol name from `ENTRY(symbol)`.
    pub entry: Option<String>,

    /// Output section definitions from `SECTIONS { }` block.
    /// Ordered as specified in the script (order matters for layout).
    pub sections: Vec<ScriptSection>,

    /// Memory region definitions from `MEMORY { }` block.
    pub memory_regions: Vec<MemoryRegion>,

    /// Symbol definitions from `PROVIDE(symbol = expr)`.
    /// These symbols are created only if they are otherwise undefined.
    pub provide_symbols: Vec<ProvideSymbol>,

    /// Wildcard patterns from `KEEP(...)` directives.
    /// Sections matching these patterns are retained even with `--gc-sections`.
    pub keep_patterns: Vec<String>,
}

/// An output section definition from a SECTIONS block.
///
/// Corresponds to a single output section rule like:
/// `.text : { *(.text) *(.text.*) }`
#[derive(Debug)]
#[allow(dead_code)] // All fields are part of public API for downstream linker consumers
pub struct ScriptSection {
    /// Output section name (e.g., ".text", ".rodata", ".data").
    pub name: String,

    /// Optional fixed virtual address for this section.
    /// From syntax like `.text 0x80000000 : { ... }`
    pub address: Option<u64>,

    /// Input section patterns that go into this output section.
    /// Each entry is a (file_pattern, section_patterns) pair.
    pub input_patterns: Vec<InputPattern>,

    /// Whether this entire output section is wrapped in KEEP().
    pub keep: bool,

    /// Optional memory region assignment (e.g., `> FLASH`).
    pub memory_region: Option<String>,
}

/// An input section pattern from a SECTIONS rule.
///
/// Represents patterns like `*(.text)`, `*(.text.*)`, `KEEP(*(.init_array))`,
/// or `file.o(.data)`.
#[derive(Debug)]
pub struct InputPattern {
    /// File pattern (usually "*" for all files, or a specific filename).
    pub file_pattern: String,

    /// Section name patterns (e.g., `[".text", ".text.*"]`).
    pub section_patterns: Vec<String>,

    /// Whether this pattern is wrapped in KEEP().
    pub keep: bool,
}

/// A memory region definition from a MEMORY block.
///
/// Represents: `name (attr) : ORIGIN = addr, LENGTH = len`
#[derive(Debug)]
pub struct MemoryRegion {
    /// Region name (e.g., "FLASH", "RAM").
    pub name: String,

    /// Region attributes string (e.g., "rx", "rw", "rwx").
    /// Optional — not all MEMORY entries have attributes.
    pub attributes: Option<String>,

    /// Starting address of the region.
    pub origin: u64,

    /// Size of the region in bytes.
    pub length: u64,
}

/// A PROVIDE or PROVIDE_HIDDEN symbol definition.
///
/// Created from `PROVIDE(symbol = expr)` or `PROVIDE_HIDDEN(symbol = expr)`.
/// The symbol is only defined if it is otherwise undefined at link time.
#[derive(Debug)]
pub struct ProvideSymbol {
    /// Symbol name.
    pub name: String,

    /// Expression defining the symbol's value.
    pub expr: SymbolExpr,

    /// Whether this is PROVIDE_HIDDEN (symbol gets STV_HIDDEN visibility).
    pub hidden: bool,
}

/// A simple expression used in PROVIDE and address assignments.
///
/// Supports a subset of GNU ld expressions sufficient for kernel linker scripts:
/// - Integer constants
/// - Symbol references
/// - Binary addition/subtraction with constants
/// - The `.` (dot) current location counter
/// - Bitwise AND / NOT (for alignment masks)
/// - ALIGN() function
#[derive(Debug)]
#[allow(dead_code)] // All variants are part of public API; some constructed by parser, consumed by linker
pub enum SymbolExpr {
    /// Integer constant (e.g., `0x80000000`, `4096`).
    Constant(u64),

    /// Symbol reference (e.g., `_start`, `__bss_start`).
    Symbol(String),

    /// Current location counter (`.`).
    Dot,

    /// Addition: left + right.
    Add(Box<SymbolExpr>, Box<SymbolExpr>),

    /// Subtraction: left - right.
    Sub(Box<SymbolExpr>, Box<SymbolExpr>),

    /// Bitwise AND: left & right (for alignment: `. & ~0xfff`).
    And(Box<SymbolExpr>, Box<SymbolExpr>),

    /// Bitwise NOT: ~expr (for alignment masks).
    Not(Box<SymbolExpr>),

    /// ALIGN(expr) function call.
    Align(Box<SymbolExpr>),
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

/// Token type produced by the linker script tokenizer.
#[derive(Debug, Clone, PartialEq)]
enum Token {
    /// An identifier or keyword (e.g., "SECTIONS", "_start", ".text").
    Ident(String),
    /// An integer literal (already parsed to u64).
    Integer(u64),
    /// A string literal (contents without quotes).
    StringLit(String),
    /// Single-character punctuation.
    Punct(char),
}

/// Strip C-style block comments (`/* ... */`) from the source text.
///
/// GNU ld scripts commonly contain block comments (especially kernel scripts
/// generated from .lds.S files). Line comments are not standard in ld scripts
/// but we also handle `//` for robustness.
fn strip_comments(src: &str) -> String {
    let bytes = src.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'*' {
            // Block comment — skip until */
            i += 2;
            while i + 1 < bytes.len() {
                if bytes[i] == b'*' && bytes[i + 1] == b'/' {
                    i += 2;
                    break;
                }
                i += 1;
            }
            // If we hit EOF without closing, just stop (lenient).
            out.push(b' ');
        } else if i + 1 < bytes.len() && bytes[i] == b'/' && bytes[i + 1] == b'/' {
            // Line comment (non-standard but tolerated) — skip to EOL.
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
        } else {
            out.push(bytes[i]);
            i += 1;
        }
    }
    // Safety: we only replaced comment bytes with spaces; rest is unchanged
    // UTF-8 validity is preserved since input was valid UTF-8.
    String::from_utf8(out).unwrap_or_else(|_| src.to_string())
}

/// Tokenize the comment-stripped linker script source into a flat token list.
///
/// Recognizes: identifiers (including dotted section names like `.text.foo`),
/// integer literals (decimal, hex `0x...`), string literals (`"..."`), and
/// single-character punctuation tokens.
fn tokenize(src: &str) -> Vec<Token> {
    let bytes = src.as_bytes();
    let mut tokens = Vec::new();
    let mut i = 0;

    while i < bytes.len() {
        let ch = bytes[i];

        // Skip whitespace.
        if ch.is_ascii_whitespace() {
            i += 1;
            continue;
        }

        // String literal.
        if ch == b'"' {
            i += 1;
            let start = i;
            while i < bytes.len() && bytes[i] != b'"' {
                if bytes[i] == b'\\' && i + 1 < bytes.len() {
                    i += 1; // skip escaped char
                }
                i += 1;
            }
            let s = String::from_utf8_lossy(&bytes[start..i]).to_string();
            if i < bytes.len() {
                i += 1; // skip closing quote
            }
            tokens.push(Token::StringLit(s));
            continue;
        }

        // Integer literal (decimal or hex).
        // Must start with a digit to be treated as an integer here.
        if ch.is_ascii_digit() {
            let start = i;
            // Check for hex prefix.
            if ch == b'0' && i + 1 < bytes.len() && (bytes[i + 1] == b'x' || bytes[i + 1] == b'X')
            {
                i += 2;
                while i < bytes.len() && bytes[i].is_ascii_hexdigit() {
                    i += 1;
                }
            } else {
                while i < bytes.len() && bytes[i].is_ascii_digit() {
                    i += 1;
                }
            }
            let num_str = &src[start..i];
            // Handle K/M/G suffixes.
            let (num_str_trimmed, multiplier) = if i < bytes.len() {
                match bytes[i] {
                    b'K' | b'k' => {
                        i += 1;
                        (num_str, 1024u64)
                    }
                    b'M' | b'm' => {
                        i += 1;
                        (num_str, 1024 * 1024)
                    }
                    b'G' | b'g' => {
                        i += 1;
                        (num_str, 1024 * 1024 * 1024)
                    }
                    _ => (num_str, 1u64),
                }
            } else {
                (num_str, 1u64)
            };
            let value = parse_integer_str(num_str_trimmed).unwrap_or(0) * multiplier;
            tokens.push(Token::Integer(value));
            continue;
        }

        // Identifiers: start with [a-zA-Z_./\*] (section names can start with `.`,
        // wildcards with `*`, paths may contain `/`).
        if ch.is_ascii_alphabetic()
            || ch == b'_'
            || ch == b'.'
            || ch == b'*'
            || ch == b'/'
        {
            let start = i;
            i += 1;
            while i < bytes.len() {
                let c = bytes[i];
                if c.is_ascii_alphanumeric()
                    || c == b'_'
                    || c == b'.'
                    || c == b'-'
                    || c == b'*'
                    || c == b'/'
                    || c == b'$'
                {
                    i += 1;
                } else {
                    break;
                }
            }
            tokens.push(Token::Ident(src[start..i].to_string()));
            continue;
        }

        // Single-character punctuation.
        match ch {
            b'{' | b'}' | b'(' | b')' | b',' | b';' | b':' | b'=' | b'>' | b'+' | b'-'
            | b'&' | b'~' | b'|' | b'!' | b'<' => {
                tokens.push(Token::Punct(ch as char));
                i += 1;
            }
            _ => {
                // Unknown byte — skip silently for leniency.
                i += 1;
            }
        }
    }

    tokens
}

/// Parse a numeric string to u64, handling `0x` hex prefix.
fn parse_integer_str(s: &str) -> Option<u64> {
    if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
        u64::from_str_radix(hex, 16).ok()
    } else {
        s.parse::<u64>().ok()
    }
}

// ---------------------------------------------------------------------------
// Parser state
// ---------------------------------------------------------------------------

/// Internal parser state operating over a flat token stream.
struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    /// Path of the script file, used in error messages.
    script_path: String,
}

impl Parser {
    fn new(tokens: Vec<Token>, script_path: String) -> Self {
        Self {
            tokens,
            pos: 0,
            script_path,
        }
    }

    // -- Helpers ----------------------------------------------------------

    /// Return true if all tokens have been consumed.
    fn at_end(&self) -> bool {
        self.pos >= self.tokens.len()
    }

    /// Peek at the current token without consuming it.
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.pos)
    }

    /// Consume and return the current token.
    fn advance(&mut self) -> Option<Token> {
        if self.pos < self.tokens.len() {
            let tok = self.tokens[self.pos].clone();
            self.pos += 1;
            Some(tok)
        } else {
            None
        }
    }

    /// Check if the current token is a specific punctuation character.
    fn is_punct(&self, ch: char) -> bool {
        matches!(self.peek(), Some(Token::Punct(c)) if *c == ch)
    }

    /// Check if the current token is a specific identifier/keyword.
    fn is_ident(&self, name: &str) -> bool {
        matches!(self.peek(), Some(Token::Ident(s)) if s == name)
    }

    /// Consume a specific punctuation character, or return an error.
    fn expect_punct(&mut self, ch: char) -> Result<(), String> {
        if self.is_punct(ch) {
            self.advance();
            Ok(())
        } else {
            let got = self.peek_description();
            Err(format!(
                "linker script '{}': expected '{}', got {} at token {}",
                self.script_path, ch, got, self.pos
            ))
        }
    }

    /// Consume an identifier token and return its string value.
    fn expect_ident(&mut self) -> Result<String, String> {
        match self.advance() {
            Some(Token::Ident(s)) => Ok(s),
            other => {
                let desc = match &other {
                    Some(Token::Punct(c)) => format!("'{}'", c),
                    Some(Token::Integer(n)) => format!("integer {}", n),
                    Some(Token::StringLit(s)) => format!("string \"{}\"", s),
                    None => "end of file".to_string(),
                    _ => "unknown".to_string(),
                };
                Err(format!(
                    "linker script '{}': expected identifier, got {} at token {}",
                    self.script_path, desc, self.pos
                ))
            }
        }
    }

    /// Consume an integer token and return its value.
    fn expect_integer(&mut self) -> Result<u64, String> {
        match self.advance() {
            Some(Token::Integer(v)) => Ok(v),
            other => {
                let desc = match &other {
                    Some(Token::Ident(s)) => format!("'{}'", s),
                    Some(Token::Punct(c)) => format!("'{}'", c),
                    None => "end of file".to_string(),
                    _ => "unknown".to_string(),
                };
                Err(format!(
                    "linker script '{}': expected integer, got {} at token {}",
                    self.script_path, desc, self.pos
                ))
            }
        }
    }

    /// Human-readable description of the current token for error messages.
    fn peek_description(&self) -> String {
        match self.peek() {
            Some(Token::Ident(s)) => format!("'{}'", s),
            Some(Token::Integer(n)) => format!("integer {}", n),
            Some(Token::Punct(c)) => format!("'{}'", c),
            Some(Token::StringLit(s)) => format!("\"{}\"", s),
            None => "end of file".to_string(),
        }
    }

    /// Skip optional semicolons (they are optional statement terminators).
    fn skip_semicolons(&mut self) {
        while self.is_punct(';') {
            self.advance();
        }
    }

    // -- Top-level parsing ------------------------------------------------

    /// Parse all top-level directives in the script.
    fn parse_script(&mut self) -> Result<LinkerScript, String> {
        let mut script = LinkerScript::default();

        while !self.at_end() {
            match self.peek() {
                Some(Token::Ident(s)) => {
                    let keyword = s.clone();
                    match keyword.as_str() {
                        "ENTRY" => self.parse_entry(&mut script)?,
                        "SECTIONS" => self.parse_sections_block(&mut script)?,
                        "MEMORY" => self.parse_memory_block(&mut script)?,
                        "PROVIDE" => self.parse_provide(&mut script, false)?,
                        "PROVIDE_HIDDEN" => self.parse_provide(&mut script, true)?,
                        "OUTPUT_FORMAT" | "OUTPUT_ARCH" | "SEARCH_DIR" | "INPUT" | "GROUP"
                        | "PHDRS" | "VERSION" | "TARGET" | "REGION_ALIAS" | "INCLUDE"
                        | "EXTERN" | "FORCE_COMMON_ALLOCATION" | "INHIBIT_COMMON_ALLOCATION"
                        | "INSERT" | "NOCROSSREFS" | "NOCROSSREFS_TO" | "OUTPUT"
                        | "STARTUP" => {
                            self.skip_directive()?;
                        }
                        "ASSERT" => {
                            self.skip_directive()?;
                        }
                        _ => {
                            // Unknown top-level token — skip it leniently.
                            self.advance();
                        }
                    }
                }
                Some(Token::Punct(';')) => {
                    self.advance();
                }
                _ => {
                    // Unknown token — skip leniently.
                    self.advance();
                }
            }
        }

        Ok(script)
    }

    /// Skip a directive by consuming through matched parentheses or to the
    /// next semicolon. Handles both `KEYWORD(...)` and `KEYWORD ...;` forms.
    fn skip_directive(&mut self) -> Result<(), String> {
        // Consume the keyword.
        self.advance();

        // If followed by `(`, skip matched parens.
        if self.is_punct('(') {
            self.skip_balanced_parens()?;
        } else {
            // Skip to semicolon or end.
            while !self.at_end() && !self.is_punct(';') {
                self.advance();
            }
        }
        self.skip_semicolons();
        Ok(())
    }

    /// Skip a balanced `(...)` group, including nested parens.
    fn skip_balanced_parens(&mut self) -> Result<(), String> {
        if !self.is_punct('(') {
            return Ok(());
        }
        self.advance(); // consume '('
        let mut depth = 1u32;
        while !self.at_end() && depth > 0 {
            match self.peek() {
                Some(Token::Punct('(')) => {
                    depth += 1;
                    self.advance();
                }
                Some(Token::Punct(')')) => {
                    depth -= 1;
                    self.advance();
                }
                _ => {
                    self.advance();
                }
            }
        }
        Ok(())
    }

    /// Skip a balanced `{...}` group, including nested braces.
    #[allow(dead_code)] // Reserved for future directive handling that uses brace groups
    fn skip_balanced_braces(&mut self) -> Result<(), String> {
        if !self.is_punct('{') {
            return Ok(());
        }
        self.advance(); // consume '{'
        let mut depth = 1u32;
        while !self.at_end() && depth > 0 {
            match self.peek() {
                Some(Token::Punct('{')) => {
                    depth += 1;
                    self.advance();
                }
                Some(Token::Punct('}')) => {
                    depth -= 1;
                    self.advance();
                }
                _ => {
                    self.advance();
                }
            }
        }
        Ok(())
    }

    // -- ENTRY ------------------------------------------------------------

    /// Parse `ENTRY(symbol)`.
    fn parse_entry(&mut self, script: &mut LinkerScript) -> Result<(), String> {
        self.advance(); // consume "ENTRY"
        self.expect_punct('(')?;
        let sym = self.expect_ident()?;
        self.expect_punct(')')?;
        script.entry = Some(sym);
        self.skip_semicolons();
        Ok(())
    }

    // -- SECTIONS ---------------------------------------------------------

    /// Parse the `SECTIONS { ... }` block.
    fn parse_sections_block(&mut self, script: &mut LinkerScript) -> Result<(), String> {
        self.advance(); // consume "SECTIONS"
        self.expect_punct('{')?;

        // Track the location counter (`. = expr`) so that subsequent output
        // sections inherit the address from the most recent dot assignment.
        // This is essential for linker scripts like:
        //   . = 0x600000;
        //   .data : { *(.data) }
        // where the .data section should be placed at 0x600000.
        let mut pending_dot_addr: Option<u64> = None;

        while !self.at_end() && !self.is_punct('}') {
            // Dot assignment: `. = expr ;`
            if self.is_ident(".") {
                // Peek ahead to distinguish `. = expr` from a section named `.xxx`.
                // If the token after "." is "=", it is a dot assignment.
                if self.pos + 1 < self.tokens.len() && self.tokens[self.pos + 1] == Token::Punct('=')
                {
                    pending_dot_addr = self.parse_dot_assignment();
                    continue;
                }
            }

            // PROVIDE / PROVIDE_HIDDEN inside SECTIONS block.
            if self.is_ident("PROVIDE") {
                self.parse_provide(script, false)?;
                continue;
            }
            if self.is_ident("PROVIDE_HIDDEN") {
                self.parse_provide(script, true)?;
                continue;
            }

            // ASSERT(...) inside SECTIONS block — skip it.
            if self.is_ident("ASSERT") {
                self.skip_directive()?;
                continue;
            }

            // Semicolons between statements.
            if self.is_punct(';') {
                self.advance();
                continue;
            }

            // Otherwise, parse an output section rule.
            // If there is a pending dot address, apply it to the next section.
            self.parse_output_section(script)?;
            if let Some(addr) = pending_dot_addr.take() {
                // Apply the pending location counter to the section we just parsed.
                // The section was just pushed, so it's the last one in the list.
                if let Some(sec) = script.sections.last_mut() {
                    if sec.address.is_none() {
                        sec.address = Some(addr);
                    }
                }
            }
        }

        if self.is_punct('}') {
            self.advance();
        }
        Ok(())
    }

    /// Parse a dot assignment: `. = expr ;`
    ///
    /// Returns the evaluated address if the expression is a simple constant,
    /// so that the `parse_sections_block` caller can propagate the location
    /// counter to subsequent output sections.
    fn parse_dot_assignment(&mut self) -> Option<u64> {
        self.advance(); // consume "."
        if self.expect_punct('=').is_err() { return None; }
        let expr = match self.parse_symbol_expr() {
            Ok(e) => e,
            Err(_) => { self.skip_semicolons(); return None; }
        };
        self.skip_semicolons();

        // Evaluate simple constant expressions to propagate the location counter.
        // More complex expressions (involving `.` or symbols) cannot be resolved
        // at parse time and are ignored — the linker's layout pass handles them.
        eval_constant_expr(&expr)
    }

    /// Parse an output section definition:
    ///
    /// ```text
    /// .text [address] : [AT(lma)] { input_patterns... } [> memory_region]
    /// ```
    fn parse_output_section(&mut self, script: &mut LinkerScript) -> Result<(), String> {
        // Section name (e.g., ".text", ".rodata", "/DISCARD/").
        let name = match self.advance() {
            Some(Token::Ident(s)) => s,
            Some(Token::Punct('.')) => {
                // This shouldn't happen due to the earlier dot-assignment check,
                // but handle gracefully.
                ".".to_string()
            }
            other => {
                let desc = match &other {
                    Some(t) => format!("{:?}", t),
                    None => "end of file".to_string(),
                };
                return Err(format!(
                    "linker script '{}': expected section name, got {} at token {}",
                    self.script_path, desc, self.pos
                ));
            }
        };

        // Optional address before the colon.
        let address = if let Some(Token::Integer(_)) = self.peek() {
            if let Some(Token::Integer(v)) = self.advance() {
                Some(v)
            } else {
                None
            }
        } else {
            None
        };

        // Expect `:` separator.
        if self.is_punct(':') {
            self.advance();
        } else {
            // Some scripts omit the colon for /DISCARD/ sections — be lenient.
            // If we see a `{` directly, just proceed.
            if !self.is_punct('{') {
                // Skip tokens until we find `{` or `;` or `}`.
                while !self.at_end()
                    && !self.is_punct('{')
                    && !self.is_punct(';')
                    && !self.is_punct('}')
                {
                    self.advance();
                }
                if self.is_punct(';') {
                    self.advance();
                    return Ok(());
                }
                if self.is_punct('}') {
                    return Ok(());
                }
            }
        }

        // Optional AT(lma) — skip the address expression.
        if self.is_ident("AT") {
            self.advance(); // consume AT
            if self.is_punct('(') {
                self.skip_balanced_parens()?;
            }
        }

        // Optional ALIGN(...) before the brace — skip.
        if self.is_ident("ALIGN") {
            self.advance();
            if self.is_punct('(') {
                self.skip_balanced_parens()?;
            }
        }

        // Optional (NOLOAD) type specifier.
        if self.is_punct('(') {
            // Check if it's a type specifier like (NOLOAD), (COPY), etc.
            let saved = self.pos;
            self.advance(); // consume '('
            if self.is_ident("NOLOAD")
                || self.is_ident("COPY")
                || self.is_ident("INFO")
                || self.is_ident("OVERLAY")
            {
                self.advance(); // consume type keyword
                if self.is_punct(')') {
                    self.advance();
                }
            } else {
                // Not a type specifier — restore position.
                self.pos = saved;
            }
        }

        // The `{` begins the input section list.
        if !self.is_punct('{') {
            // Lenient: if no brace, skip to next statement boundary.
            while !self.at_end() && !self.is_punct(';') && !self.is_punct('}') {
                self.advance();
            }
            self.skip_semicolons();
            return Ok(());
        }
        self.advance(); // consume '{'

        let mut section = ScriptSection {
            name,
            address,
            input_patterns: Vec::new(),
            keep: false,
            memory_region: None,
        };

        // Parse input section patterns until closing '}'.
        while !self.at_end() && !self.is_punct('}') {
            // KEEP(...) wrapping input patterns.
            if self.is_ident("KEEP") {
                self.advance(); // consume "KEEP"
                self.expect_punct('(')?;
                // Inside KEEP(), we may find one or more input patterns.
                while !self.at_end() && !self.is_punct(')') {
                    if let Some(pattern) = self.try_parse_input_pattern()? {
                        // Record the keep pattern for --gc-sections.
                        for sp in &pattern.section_patterns {
                            script.keep_patterns.push(sp.clone());
                        }
                        section.input_patterns.push(InputPattern {
                            file_pattern: pattern.file_pattern,
                            section_patterns: pattern.section_patterns,
                            keep: true,
                        });
                    } else {
                        break;
                    }
                }
                if self.is_punct(')') {
                    self.advance();
                }
                self.skip_semicolons();
                continue;
            }

            // PROVIDE / PROVIDE_HIDDEN inside a section body.
            if self.is_ident("PROVIDE") {
                self.parse_provide(script, false)?;
                continue;
            }
            if self.is_ident("PROVIDE_HIDDEN") {
                self.parse_provide(script, true)?;
                continue;
            }

            // Dot assignment inside section body: `. = expr ;`
            if self.is_ident(".") {
                if self.pos + 1 < self.tokens.len()
                    && self.tokens[self.pos + 1] == Token::Punct('=')
                {
                    let _ = self.parse_dot_assignment();
                    continue;
                }
            }

            // SORT_BY_NAME(...), SORT(...), SORT_BY_ALIGNMENT(...), CONSTRUCTORS,
            // CREATE_OBJECT_SYMBOLS — skip these special directives inside sections.
            if self.is_ident("SORT_BY_NAME")
                || self.is_ident("SORT")
                || self.is_ident("SORT_BY_ALIGNMENT")
                || self.is_ident("SORT_BY_INIT_PRIORITY")
                || self.is_ident("SORT_NONE")
            {
                self.advance();
                if self.is_punct('(') {
                    // Parse inner content as input patterns.
                    self.advance(); // consume '('
                    while !self.at_end() && !self.is_punct(')') {
                        if let Some(pattern) = self.try_parse_input_pattern()? {
                            section.input_patterns.push(pattern);
                        } else {
                            break;
                        }
                    }
                    if self.is_punct(')') {
                        self.advance();
                    }
                }
                self.skip_semicolons();
                continue;
            }

            if self.is_ident("CONSTRUCTORS") || self.is_ident("CREATE_OBJECT_SYMBOLS") {
                self.advance();
                self.skip_semicolons();
                continue;
            }

            // Fill expression: `FILL(0x...)` — skip.
            if self.is_ident("FILL") {
                self.advance();
                if self.is_punct('(') {
                    self.skip_balanced_parens()?;
                }
                self.skip_semicolons();
                continue;
            }

            // Byte/Short/Long/Quad insertion — skip.
            if self.is_ident("BYTE")
                || self.is_ident("SHORT")
                || self.is_ident("LONG")
                || self.is_ident("QUAD")
                || self.is_ident("SQUAD")
            {
                self.advance();
                if self.is_punct('(') {
                    self.skip_balanced_parens()?;
                }
                self.skip_semicolons();
                continue;
            }

            // Semicolons.
            if self.is_punct(';') {
                self.advance();
                continue;
            }

            // ASSERT inside section body — skip.
            if self.is_ident("ASSERT") {
                self.skip_directive()?;
                continue;
            }

            // Try to parse an input section pattern.
            if let Some(pattern) = self.try_parse_input_pattern()? {
                section.input_patterns.push(pattern);
            } else {
                // Cannot parse — skip token leniently.
                self.advance();
            }
        }

        // Consume closing '}'.
        if self.is_punct('}') {
            self.advance();
        }

        // Optional `> memory_region` after closing brace.
        if self.is_punct('>') {
            self.advance();
            if let Some(Token::Ident(region)) = self.peek().cloned() {
                self.advance();
                section.memory_region = Some(region);
            }
        }

        // Optional `AT > lma_region` — skip.
        if self.is_ident("AT") {
            self.advance();
            if self.is_punct('>') {
                self.advance();
                // Consume region name.
                if let Some(Token::Ident(_)) = self.peek() {
                    self.advance();
                }
            }
        }

        // Optional `=fill` expression — skip.
        if self.is_punct('=') {
            self.advance();
            // Consume fill value.
            match self.peek() {
                Some(Token::Integer(_)) | Some(Token::Ident(_)) => {
                    self.advance();
                }
                _ => {}
            }
        }

        self.skip_semicolons();
        script.sections.push(section);
        Ok(())
    }

    /// Try to parse an input section pattern like `*(.text)` or `file.o(.data .data.*)`.
    ///
    /// Returns `None` if the current position does not look like a valid input
    /// pattern (allowing the caller to skip leniently).
    fn try_parse_input_pattern(&mut self) -> Result<Option<InputPattern>, String> {
        // The file pattern is an identifier (e.g., "*", "*.o", "crtbegin.o").
        let file_pattern = match self.peek() {
            Some(Token::Ident(_)) => {
                if let Some(Token::Ident(s)) = self.advance() {
                    s
                } else {
                    return Ok(None);
                }
            }
            _ => return Ok(None),
        };

        // If followed by `(`, parse section patterns inside parentheses.
        if self.is_punct('(') {
            self.advance(); // consume '('
            let mut section_patterns = Vec::new();
            while !self.at_end() && !self.is_punct(')') {
                // Handle SORT_BY_NAME/SORT/EXCLUDE_FILE inside the pattern.
                if self.is_ident("SORT_BY_NAME")
                    || self.is_ident("SORT")
                    || self.is_ident("SORT_BY_ALIGNMENT")
                    || self.is_ident("SORT_BY_INIT_PRIORITY")
                    || self.is_ident("SORT_NONE")
                    || self.is_ident("EXCLUDE_FILE")
                {
                    self.advance(); // consume directive name
                    if self.is_punct('(') {
                        // Parse inner patterns.
                        self.advance(); // consume '('
                        while !self.at_end() && !self.is_punct(')') {
                            match self.advance() {
                                Some(Token::Ident(s)) => {
                                    section_patterns.push(s);
                                }
                                _ => {}
                            }
                        }
                        if self.is_punct(')') {
                            self.advance();
                        }
                    }
                    continue;
                }

                match self.peek() {
                    Some(Token::Ident(_)) => {
                        if let Some(Token::Ident(s)) = self.advance() {
                            section_patterns.push(s);
                        }
                    }
                    _ => {
                        // Skip unexpected tokens inside pattern.
                        self.advance();
                    }
                }
            }
            if self.is_punct(')') {
                self.advance();
            }
            self.skip_semicolons();
            Ok(Some(InputPattern {
                file_pattern,
                section_patterns,
                keep: false,
            }))
        } else {
            // Bare identifier without parentheses — treat as a symbol or special
            // keyword. In real linker scripts, bare identifiers inside section bodies
            // are typically symbol assignments; we skip them leniently.
            self.skip_semicolons();
            Ok(None)
        }
    }

    // -- MEMORY -----------------------------------------------------------

    /// Parse the `MEMORY { ... }` block.
    fn parse_memory_block(&mut self, script: &mut LinkerScript) -> Result<(), String> {
        self.advance(); // consume "MEMORY"
        self.expect_punct('{')?;

        while !self.at_end() && !self.is_punct('}') {
            // Region name.
            let name = match self.peek() {
                Some(Token::Ident(_)) => self.expect_ident()?,
                _ => {
                    // Skip unexpected tokens leniently.
                    self.advance();
                    continue;
                }
            };

            // Optional attributes: `(rwx)`.
            let attributes = if self.is_punct('(') {
                self.advance(); // consume '('
                let mut attr = String::new();
                while !self.at_end() && !self.is_punct(')') {
                    match self.advance() {
                        Some(Token::Ident(s)) => attr.push_str(&s),
                        Some(Token::Punct(c)) if c != ')' => attr.push(c),
                        _ => {}
                    }
                }
                if self.is_punct(')') {
                    self.advance();
                }
                if attr.is_empty() { None } else { Some(attr) }
            } else {
                None
            };

            // Expect `:` separator.
            if self.is_punct(':') {
                self.advance();
            }

            // ORIGIN = value
            let origin = self.parse_memory_origin()?;

            // Comma separator.
            if self.is_punct(',') {
                self.advance();
            }

            // LENGTH = value
            let length = self.parse_memory_length()?;

            script.memory_regions.push(MemoryRegion {
                name,
                attributes,
                origin,
                length,
            });

            self.skip_semicolons();
        }

        if self.is_punct('}') {
            self.advance();
        }
        Ok(())
    }

    /// Parse `ORIGIN = value` or `org = value` or `o = value`.
    fn parse_memory_origin(&mut self) -> Result<u64, String> {
        match self.peek() {
            Some(Token::Ident(s))
                if s == "ORIGIN" || s == "org" || s == "o" =>
            {
                self.advance();
            }
            _ => {
                return Err(format!(
                    "linker script '{}': expected ORIGIN/org/o, got {} at token {}",
                    self.script_path,
                    self.peek_description(),
                    self.pos
                ));
            }
        }
        self.expect_punct('=')?;
        self.parse_expr_integer()
    }

    /// Parse `LENGTH = value` or `len = value` or `l = value`.
    fn parse_memory_length(&mut self) -> Result<u64, String> {
        match self.peek() {
            Some(Token::Ident(s))
                if s == "LENGTH" || s == "len" || s == "l" =>
            {
                self.advance();
            }
            _ => {
                return Err(format!(
                    "linker script '{}': expected LENGTH/len/l, got {} at token {}",
                    self.script_path,
                    self.peek_description(),
                    self.pos
                ));
            }
        }
        self.expect_punct('=')?;
        self.parse_expr_integer()
    }

    /// Parse an integer expression (may be a simple constant or a simple
    /// arithmetic expression that evaluates to a constant at parse time).
    fn parse_expr_integer(&mut self) -> Result<u64, String> {
        match self.peek() {
            Some(Token::Integer(_)) => self.expect_integer(),
            Some(Token::Ident(s)) if s.starts_with("0x") || s.starts_with("0X") => {
                let s = s.clone();
                self.advance();
                parse_integer_str(&s).ok_or_else(|| {
                    format!(
                        "linker script '{}': invalid integer '{}'",
                        self.script_path, s
                    )
                })
            }
            _ => self.expect_integer(),
        }
    }

    // -- PROVIDE / PROVIDE_HIDDEN -----------------------------------------

    /// Parse `PROVIDE(symbol = expr)` or `PROVIDE_HIDDEN(symbol = expr)`.
    fn parse_provide(&mut self, script: &mut LinkerScript, hidden: bool) -> Result<(), String> {
        self.advance(); // consume "PROVIDE" or "PROVIDE_HIDDEN"
        self.expect_punct('(')?;
        let name = self.expect_ident()?;
        self.expect_punct('=')?;
        let expr = self.parse_symbol_expr()?;
        self.expect_punct(')')?;
        self.skip_semicolons();
        script.provide_symbols.push(ProvideSymbol {
            name,
            expr,
            hidden,
        });
        Ok(())
    }

    // -- Expression parser ------------------------------------------------

    /// Parse a symbol expression (additive level).
    ///
    /// ```text
    /// expr     = bitand { ('+' | '-') bitand }
    /// bitand   = unary { '&' unary }
    /// unary    = '~' primary | 'ALIGN' '(' expr ')' | primary
    /// primary  = '(' expr ')' | '.' | INTEGER | IDENTIFIER
    /// ```
    fn parse_symbol_expr(&mut self) -> Result<SymbolExpr, String> {
        self.parse_expr_additive()
    }

    /// Parse additive expressions: `left (+|-) right`.
    fn parse_expr_additive(&mut self) -> Result<SymbolExpr, String> {
        let mut left = self.parse_expr_bitand()?;
        loop {
            if self.is_punct('+') {
                self.advance();
                let right = self.parse_expr_bitand()?;
                left = SymbolExpr::Add(Box::new(left), Box::new(right));
            } else if self.is_punct('-') {
                self.advance();
                let right = self.parse_expr_bitand()?;
                left = SymbolExpr::Sub(Box::new(left), Box::new(right));
            } else {
                break;
            }
        }
        Ok(left)
    }

    /// Parse bitwise AND expressions: `left & right`.
    fn parse_expr_bitand(&mut self) -> Result<SymbolExpr, String> {
        let mut left = self.parse_expr_unary()?;
        while self.is_punct('&') {
            self.advance();
            let right = self.parse_expr_unary()?;
            left = SymbolExpr::And(Box::new(left), Box::new(right));
        }
        Ok(left)
    }

    /// Parse unary expressions: `~expr`, `ALIGN(expr)`, or a primary.
    fn parse_expr_unary(&mut self) -> Result<SymbolExpr, String> {
        if self.is_punct('~') {
            self.advance();
            let inner = self.parse_expr_primary()?;
            return Ok(SymbolExpr::Not(Box::new(inner)));
        }
        if self.is_ident("ALIGN") {
            self.advance();
            self.expect_punct('(')?;
            let inner = self.parse_symbol_expr()?;
            self.expect_punct(')')?;
            return Ok(SymbolExpr::Align(Box::new(inner)));
        }
        self.parse_expr_primary()
    }

    /// Parse a primary expression: `(expr)`, `.`, integer, or identifier.
    fn parse_expr_primary(&mut self) -> Result<SymbolExpr, String> {
        // Parenthesized sub-expression.
        if self.is_punct('(') {
            self.advance();
            let inner = self.parse_symbol_expr()?;
            self.expect_punct(')')?;
            return Ok(inner);
        }

        // Dot — current location counter.
        if self.is_ident(".") {
            self.advance();
            return Ok(SymbolExpr::Dot);
        }

        // Integer literal.
        if let Some(Token::Integer(_)) = self.peek() {
            let v = self.expect_integer()?;
            return Ok(SymbolExpr::Constant(v));
        }

        // Identifier (symbol reference).
        if let Some(Token::Ident(_)) = self.peek() {
            let s = self.expect_ident()?;
            // Could be a hex number that the tokenizer treated as an ident.
            if let Some(v) = parse_integer_str(&s) {
                return Ok(SymbolExpr::Constant(v));
            }
            return Ok(SymbolExpr::Symbol(s));
        }

        Err(format!(
            "linker script '{}': expected expression, got {} at token {}",
            self.script_path,
            self.peek_description(),
            self.pos
        ))
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Parse a linker script file and return the structured representation.
///
/// Reads the file at `path`, tokenizes it, and parses all directives:
/// SECTIONS, MEMORY, ENTRY, PROVIDE, PROVIDE_HIDDEN, and KEEP.
///
/// Returns `Ok(LinkerScript)` on success, or `Err(String)` with a
/// human-readable error message on parse failure.
/// Evaluate a `SymbolExpr` that contains only constants and basic arithmetic.
///
/// Returns `None` for expressions that require runtime context (symbol
/// references, `.` dot location counter). Used by `parse_dot_assignment` to
/// resolve simple location counter assignments like `. = 0x600000`.
fn eval_constant_expr(expr: &SymbolExpr) -> Option<u64> {
    match expr {
        SymbolExpr::Constant(v) => Some(*v),
        SymbolExpr::Add(l, r) => {
            Some(eval_constant_expr(l)?.wrapping_add(eval_constant_expr(r)?))
        }
        SymbolExpr::Sub(l, r) => {
            Some(eval_constant_expr(l)?.wrapping_sub(eval_constant_expr(r)?))
        }
        SymbolExpr::And(l, r) => {
            Some(eval_constant_expr(l)? & eval_constant_expr(r)?)
        }
        SymbolExpr::Not(inner) => Some(!eval_constant_expr(inner)?),
        SymbolExpr::Align(inner) => eval_constant_expr(inner),
        SymbolExpr::Symbol(_) | SymbolExpr::Dot => None,
    }
}

pub fn parse_linker_script(path: &Path) -> Result<LinkerScript, String> {
    let content = std::fs::read_to_string(path).map_err(|e| {
        format!(
            "failed to read linker script '{}': {}",
            path.display(),
            e
        )
    })?;
    parse_linker_script_from_str(&content, &path.display().to_string())
}

/// Parse a linker script from an in-memory string.
///
/// This is the internal entry point used by both `parse_linker_script()` (from
/// a file path) and unit tests (from string literals).
fn parse_linker_script_from_str(
    content: &str,
    script_path: &str,
) -> Result<LinkerScript, String> {
    let stripped = strip_comments(content);
    let tokens = tokenize(&stripped);
    let mut parser = Parser::new(tokens, script_path.to_string());
    parser.parse_script()
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_entry() {
        let script = parse_linker_script_from_str("ENTRY(_start)", "<test>").unwrap();
        assert_eq!(script.entry, Some("_start".to_string()));
    }

    #[test]
    fn test_parse_entry_with_semicolon() {
        let script = parse_linker_script_from_str("ENTRY(_start);", "<test>").unwrap();
        assert_eq!(script.entry, Some("_start".to_string()));
    }

    #[test]
    fn test_parse_sections_basic() {
        let input = r#"
            SECTIONS {
                .text : { *(.text) *(.text.*) }
                .rodata : { *(.rodata) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 2);
        assert_eq!(script.sections[0].name, ".text");
        assert_eq!(script.sections[0].input_patterns.len(), 2);
        assert_eq!(script.sections[0].input_patterns[0].file_pattern, "*");
        assert_eq!(
            script.sections[0].input_patterns[0].section_patterns,
            vec![".text"]
        );
        assert_eq!(
            script.sections[0].input_patterns[1].section_patterns,
            vec![".text.*"]
        );
        assert_eq!(script.sections[1].name, ".rodata");
        assert_eq!(script.sections[1].input_patterns.len(), 1);
    }

    #[test]
    fn test_parse_sections_with_address() {
        let input = r#"
            SECTIONS {
                .text 0x80000000 : { *(.text) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
        assert_eq!(script.sections[0].address, Some(0x80000000));
    }

    #[test]
    fn test_parse_sections_with_keep() {
        let input = r#"
            SECTIONS {
                .init.data : {
                    KEEP(*(.init.data))
                    KEEP(*(.init.text))
                }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
        assert_eq!(script.sections[0].input_patterns.len(), 2);
        assert!(script.sections[0].input_patterns[0].keep);
        assert!(script.sections[0].input_patterns[1].keep);
        assert!(script.keep_patterns.contains(&".init.data".to_string()));
        assert!(script.keep_patterns.contains(&".init.text".to_string()));
    }

    #[test]
    fn test_parse_sections_with_provide_inside() {
        let input = r#"
            SECTIONS {
                .bss : {
                    PROVIDE(__bss_start = .);
                    *(.bss)
                    *(.bss.*)
                    *(COMMON)
                    PROVIDE(__bss_end = .);
                }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
        assert_eq!(script.provide_symbols.len(), 2);
        assert_eq!(script.provide_symbols[0].name, "__bss_start");
        assert!(!script.provide_symbols[0].hidden);
        assert_eq!(script.provide_symbols[1].name, "__bss_end");
        // .bss section should have input patterns for .bss, .bss.*, and COMMON.
        assert_eq!(script.sections[0].input_patterns.len(), 3);
    }

    #[test]
    fn test_parse_memory_block() {
        let input = r#"
            MEMORY {
                FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 256K
                RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.memory_regions.len(), 2);
        assert_eq!(script.memory_regions[0].name, "FLASH");
        assert_eq!(script.memory_regions[0].attributes, Some("rx".to_string()));
        assert_eq!(script.memory_regions[0].origin, 0x08000000);
        assert_eq!(script.memory_regions[0].length, 256 * 1024);
        assert_eq!(script.memory_regions[1].name, "RAM");
        assert_eq!(script.memory_regions[1].attributes, Some("rwx".to_string()));
        assert_eq!(script.memory_regions[1].origin, 0x20000000);
        assert_eq!(script.memory_regions[1].length, 64 * 1024);
    }

    #[test]
    fn test_parse_memory_origin_aliases() {
        let input = r#"
            MEMORY {
                ROM (rx) : org = 0x0, len = 0x10000
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.memory_regions.len(), 1);
        assert_eq!(script.memory_regions[0].origin, 0x0);
        assert_eq!(script.memory_regions[0].length, 0x10000);
    }

    #[test]
    fn test_parse_provide() {
        let input = r#"
            PROVIDE(__stack_top = 0x20010000);
            PROVIDE_HIDDEN(__hidden_sym = __bss_end + 4);
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.provide_symbols.len(), 2);
        assert_eq!(script.provide_symbols[0].name, "__stack_top");
        assert!(!script.provide_symbols[0].hidden);
        assert_eq!(script.provide_symbols[1].name, "__hidden_sym");
        assert!(script.provide_symbols[1].hidden);
    }

    #[test]
    fn test_parse_provide_hidden_inside_sections() {
        let input = r#"
            SECTIONS {
                .data : {
                    PROVIDE_HIDDEN(__data_start = .);
                    *(.data)
                    PROVIDE_HIDDEN(__data_end = .);
                }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.provide_symbols.len(), 2);
        assert!(script.provide_symbols[0].hidden);
        assert!(script.provide_symbols[1].hidden);
    }

    #[test]
    fn test_strip_comments() {
        let input = "/* header comment */\nENTRY(_start) /* inline */\n/* end */";
        let stripped = strip_comments(input);
        assert!(!stripped.contains("header comment"));
        assert!(!stripped.contains("inline"));
        assert!(stripped.contains("ENTRY"));
        assert!(stripped.contains("_start"));
    }

    #[test]
    fn test_strip_line_comments() {
        let input = "ENTRY(_start) // this is a line comment\nSECTIONS { }";
        let stripped = strip_comments(input);
        assert!(!stripped.contains("line comment"));
        assert!(stripped.contains("ENTRY"));
        assert!(stripped.contains("SECTIONS"));
    }

    #[test]
    fn test_hex_integer_parsing() {
        assert_eq!(parse_integer_str("0x80000000"), Some(0x80000000));
        assert_eq!(parse_integer_str("0XABCDEF"), Some(0xABCDEF));
        assert_eq!(parse_integer_str("4096"), Some(4096));
        assert_eq!(parse_integer_str("0"), Some(0));
    }

    #[test]
    fn test_expression_constant() {
        let input = "PROVIDE(sym = 0x1000);";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.provide_symbols.len(), 1);
        match &script.provide_symbols[0].expr {
            SymbolExpr::Constant(v) => assert_eq!(*v, 0x1000),
            other => panic!("expected Constant, got {:?}", other),
        }
    }

    #[test]
    fn test_expression_dot() {
        let input = "PROVIDE(sym = .);";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        match &script.provide_symbols[0].expr {
            SymbolExpr::Dot => {}
            other => panic!("expected Dot, got {:?}", other),
        }
    }

    #[test]
    fn test_expression_add_sub() {
        let input = "PROVIDE(sym = __start + 0x100);";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        match &script.provide_symbols[0].expr {
            SymbolExpr::Add(left, right) => {
                match left.as_ref() {
                    SymbolExpr::Symbol(s) => assert_eq!(s, "__start"),
                    other => panic!("expected Symbol, got {:?}", other),
                }
                match right.as_ref() {
                    SymbolExpr::Constant(v) => assert_eq!(*v, 0x100),
                    other => panic!("expected Constant, got {:?}", other),
                }
            }
            other => panic!("expected Add, got {:?}", other),
        }
    }

    #[test]
    fn test_expression_bitwise_and_not() {
        let input = "PROVIDE(aligned = . & ~0xFFF);";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        match &script.provide_symbols[0].expr {
            SymbolExpr::And(left, right) => {
                match left.as_ref() {
                    SymbolExpr::Dot => {}
                    other => panic!("expected Dot, got {:?}", other),
                }
                match right.as_ref() {
                    SymbolExpr::Not(inner) => match inner.as_ref() {
                        SymbolExpr::Constant(v) => assert_eq!(*v, 0xFFF),
                        other => panic!("expected Constant, got {:?}", other),
                    },
                    other => panic!("expected Not, got {:?}", other),
                }
            }
            other => panic!("expected And, got {:?}", other),
        }
    }

    #[test]
    fn test_expression_align() {
        let input = "PROVIDE(aligned = ALIGN(0x1000));";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        match &script.provide_symbols[0].expr {
            SymbolExpr::Align(inner) => match inner.as_ref() {
                SymbolExpr::Constant(v) => assert_eq!(*v, 0x1000),
                other => panic!("expected Constant, got {:?}", other),
            },
            other => panic!("expected Align, got {:?}", other),
        }
    }

    #[test]
    fn test_memory_region_assignment() {
        let input = r#"
            MEMORY {
                FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
            }
            SECTIONS {
                .text : { *(.text) } > FLASH
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections[0].memory_region, Some("FLASH".to_string()));
    }

    #[test]
    fn test_kernel_like_script() {
        let input = r#"
            /* vmlinux.lds — simplified kernel linker script */
            ENTRY(_start)
            
            SECTIONS {
                . = 0xffffffff80000000;
                
                .text : {
                    *(.text)
                    *(.text.*)
                }
                
                .rodata : {
                    *(.rodata)
                    *(.rodata.*)
                }
                
                .init.data : {
                    PROVIDE(__init_begin = .);
                    KEEP(*(.init.data))
                    KEEP(*(.init.text))
                    PROVIDE(__init_end = .);
                }
                
                .data : {
                    *(.data)
                    *(.data.*)
                }
                
                .bss : {
                    PROVIDE(__bss_start = .);
                    *(.bss)
                    *(.bss.*)
                    *(COMMON)
                    PROVIDE(__bss_end = .);
                }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.entry, Some("_start".to_string()));
        assert_eq!(script.sections.len(), 5);
        assert_eq!(script.sections[0].name, ".text");
        assert_eq!(script.sections[1].name, ".rodata");
        assert_eq!(script.sections[2].name, ".init.data");
        assert_eq!(script.sections[3].name, ".data");
        assert_eq!(script.sections[4].name, ".bss");
        // Check KEEP patterns.
        assert!(script.keep_patterns.contains(&".init.data".to_string()));
        assert!(script.keep_patterns.contains(&".init.text".to_string()));
        // Check PROVIDE symbols.
        assert_eq!(script.provide_symbols.len(), 4);
    }

    #[test]
    fn test_unknown_directives_skipped() {
        let input = r#"
            OUTPUT_FORMAT("elf64-x86-64")
            OUTPUT_ARCH(i386:x86-64)
            SEARCH_DIR("/usr/lib")
            ENTRY(_start)
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.entry, Some("_start".to_string()));
    }

    #[test]
    fn test_nested_braces_in_sections() {
        let input = r#"
            SECTIONS {
                .text : {
                    *(.text)
                }
                .data : {
                    *(.data)
                }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 2);
    }

    #[test]
    fn test_multiple_section_patterns() {
        let input = r#"
            SECTIONS {
                .text : { *(.text .text.* .gnu.linkonce.t.*) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections[0].input_patterns.len(), 1);
        assert_eq!(
            script.sections[0].input_patterns[0].section_patterns,
            vec![".text", ".text.*", ".gnu.linkonce.t.*"]
        );
    }

    #[test]
    fn test_dot_assignment_in_sections() {
        let input = r#"
            SECTIONS {
                . = 0x10000;
                .text : { *(.text) }
                . = 0x20000;
                .data : { *(.data) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 2);
    }

    #[test]
    fn test_empty_script() {
        let script = parse_linker_script_from_str("", "<test>").unwrap();
        assert_eq!(script.entry, None);
        assert!(script.sections.is_empty());
        assert!(script.memory_regions.is_empty());
        assert!(script.provide_symbols.is_empty());
        assert!(script.keep_patterns.is_empty());
    }

    #[test]
    fn test_file_read_error() {
        let result = parse_linker_script(Path::new("/nonexistent/path/script.ld"));
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert!(err.contains("failed to read linker script"));
    }

    #[test]
    fn test_discard_section() {
        let input = r#"
            SECTIONS {
                /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
        assert_eq!(script.sections[0].name, "/DISCARD/");
    }

    #[test]
    fn test_memory_m_suffix() {
        let input = r#"
            MEMORY {
                ROM (rx) : ORIGIN = 0x0, LENGTH = 1M
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.memory_regions[0].length, 1024 * 1024);
    }

    #[test]
    fn test_noload_section() {
        let input = r#"
            SECTIONS {
                .bss (NOLOAD) : { *(.bss) }
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
        assert_eq!(script.sections[0].name, ".bss");
    }

    #[test]
    fn test_section_with_fill() {
        let input = r#"
            SECTIONS {
                .text : { *(.text) } =0x90909090
            }
        "#;
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.sections.len(), 1);
    }

    #[test]
    fn test_complex_provide_expression() {
        let input = "PROVIDE(sym = (. + 0xFF) & ~0xFF);";
        let script = parse_linker_script_from_str(input, "<test>").unwrap();
        assert_eq!(script.provide_symbols.len(), 1);
        // The expression should be And(Add(Dot, Constant(0xFF)), Not(Constant(0xFF)))
        match &script.provide_symbols[0].expr {
            SymbolExpr::And(_, _) => {} // structural match is enough
            other => panic!("expected And expression, got {:?}", other),
        }
    }

    #[test]
    fn test_tokenizer_integers() {
        let tokens = tokenize("0x1000 42 0XABCDEF 64K 1M");
        assert_eq!(tokens.len(), 5);
        assert_eq!(tokens[0], Token::Integer(0x1000));
        assert_eq!(tokens[1], Token::Integer(42));
        assert_eq!(tokens[2], Token::Integer(0xABCDEF));
        assert_eq!(tokens[3], Token::Integer(64 * 1024));
        assert_eq!(tokens[4], Token::Integer(1024 * 1024));
    }

    #[test]
    fn test_tokenizer_identifiers() {
        let tokens = tokenize("SECTIONS .text *(.text.*) _start");
        assert_eq!(tokens[0], Token::Ident("SECTIONS".to_string()));
        assert_eq!(tokens[1], Token::Ident(".text".to_string()));
        // "*(.text.*)" tokenizes as: Ident("*"), Punct('('), Ident(".text.*"), Punct(')')
        assert_eq!(tokens[2], Token::Ident("*".to_string()));
        assert_eq!(tokens[3], Token::Punct('('));
        assert_eq!(tokens[4], Token::Ident(".text.*".to_string()));
        assert_eq!(tokens[5], Token::Punct(')'));
        assert_eq!(tokens[6], Token::Ident("_start".to_string()));
    }

    #[test]
    fn test_tokenizer_punctuation() {
        let tokens = tokenize("{ } ( ) : ; = > + - & ~");
        let expected: Vec<Token> = vec![
            Token::Punct('{'),
            Token::Punct('}'),
            Token::Punct('('),
            Token::Punct(')'),
            Token::Punct(':'),
            Token::Punct(';'),
            Token::Punct('='),
            Token::Punct('>'),
            Token::Punct('+'),
            Token::Punct('-'),
            Token::Punct('&'),
            Token::Punct('~'),
        ];
        assert_eq!(tokens, expected);
    }
}
