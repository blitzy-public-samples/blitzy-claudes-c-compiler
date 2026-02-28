// Test: linker script PROVIDE() and KEEP() directives
//
// Compile: ccc -T script.ld -o test main.c
//
// This test verifies:
//   PROVIDE(provided_magic = 42)  — linker-defined symbol (not in C source)
//   PROVIDE(user_defined = 99)    — should NOT override C definition (55)
//   KEEP(*(.keep_section))        — prevents GC of .keep_section
//
// The script defines:
//   provided_magic = 42 (only if not defined by object files)
//   user_defined = 99 (but we define it as 55 in C, so PROVIDE is ignored)

int printf(const char *fmt, ...);

// Linker-provided symbol: accessed as an address whose numeric value is 42.
// We do NOT define provided_magic in C, so the linker's PROVIDE takes effect.
extern char provided_magic;

// User-defined variable: we define it here with value 55.
// The linker script has PROVIDE(user_defined = 99), but per PROVIDE semantics
// this should NOT override our definition.
int user_defined = 55;

// Place a variable in .keep_section to test KEEP directive.
// Without KEEP, --gc-sections could remove this unreferenced section.
__attribute__((section(".keep_section")))
int kept_value = 77;

int main(void) {
    // Check PROVIDE(provided_magic = 42)
    // The symbol's "address" is the numeric value 42
    unsigned long magic = (unsigned long)&provided_magic;

    // Check that user_defined retains our C value (55), not the linker's PROVIDE (99)
    int user_val = user_defined;

    // Check KEEP: kept_value should still be accessible
    int keep_val = kept_value;

    if (magic == 42 && user_val == 55 && keep_val == 77) {
        printf("PROVIDE and KEEP OK\n");
    } else {
        if (magic != 42)
            printf("FAIL: provided_magic = %lu, expected 42\n", magic);
        if (user_val != 55)
            printf("FAIL: user_defined = %d, expected 55\n", user_val);
        if (keep_val != 77)
            printf("FAIL: kept_value = %d, expected 77\n", keep_val);
    }

    return 0;
}
