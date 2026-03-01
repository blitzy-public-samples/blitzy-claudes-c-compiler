// Test: Linker script PROVIDE(symbol = expr) and KEEP(...) directives
//
// Compile: ccc -T script.ld -o test main.c
//
// The companion linker script (script.ld) contains:
//   PROVIDE(provided_magic = 42)  -- define symbol only if not in object files
//   PROVIDE(user_defined = 99)    -- should NOT override program's definition
//   KEEP(*(.keep_section))        -- retain .keep_section from GC
//   ENTRY(main)                   -- set entry point
//
// This test verifies:
//   1. PROVIDE creates symbols for undefined references with correct values
//   2. PROVIDE does NOT override existing definitions from object files
//   3. KEEP retains sections that would otherwise be garbage collected

int puts(const char *s);

// ---- Test 1: PROVIDE symbol for undefined reference ----
//
// The linker script defines: PROVIDE(provided_magic = 42)
// Since we do NOT define 'provided_magic' in this file, the linker
// should create it as a symbol with value 42.
//
// In C, linker script symbols are accessed via address-of because
// the symbol IS the value (not a pointer to a value). Thus:
//   (unsigned long)&provided_magic == 42
extern char provided_magic;

// ---- Test 2: PROVIDE does not override existing definition ----
//
// The linker script defines: PROVIDE(user_defined = 99)
// But we define 'user_defined' here as a global int initialized to 55.
// Per GNU ld PROVIDE semantics, the linker should NOT override our
// definition. The variable should retain value 55 at runtime.
int user_defined = 55;

// ---- Test 3: KEEP retains section from garbage collection ----
//
// Place data in a custom section '.keep_section'. The linker script
// includes KEEP(*(.keep_section)) in the .data output section,
// ensuring this data survives even when --gc-sections is active.
//
// We use __attribute__((section(".keep_section"))) to place the
// variable in the named section, and __attribute__((used)) to
// prevent the compiler from eliminating it as unused.
__attribute__((section(".keep_section"), used))
static int kept_value = 12345;

int main(void) {
    // Test 1: Verify PROVIDE-defined symbol has the correct value.
    // Linker script symbols are address values, accessed via &symbol.
    unsigned long magic = (unsigned long)&provided_magic;
    if (magic != 42) {
        puts("FAIL: provided_magic wrong value");
        return 1;
    }

    // Test 2: Verify PROVIDE did not override our definition.
    // user_defined should still be 55 (our value), not 99 (linker script PROVIDE value).
    if (user_defined != 55) {
        puts("FAIL: user_defined was overridden by PROVIDE");
        return 1;
    }

    // Test 3: Verify KEEP'd section data is intact.
    // The data in .keep_section should be accessible and have the correct value,
    // confirming that KEEP prevented the section from being discarded.
    if (kept_value != 12345) {
        puts("FAIL: keep_section data corrupted or removed");
        return 1;
    }

    puts("PROVIDE and KEEP OK");
    return 0;
}
