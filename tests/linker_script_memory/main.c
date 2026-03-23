// Test: Linker script MEMORY { } and ENTRY(symbol) directives
//
// Compile: ccc -T script.ld -o test main.c
//
// The companion linker script (script.ld) contains:
//   MEMORY {
//       CODE (rx)  : ORIGIN = 0x400000, LENGTH = 0x100000
//       DATA (rw)  : ORIGIN = 0x600000, LENGTH = 0x100000
//   }
//   SECTIONS {
//       .text   : { *(.text) *(.text.*) } > CODE
//       .rodata : { *(.rodata) *(.rodata.*) } > CODE
//       .data   : { *(.data) *(.data.*) } > DATA
//       .bss    : { *(.bss) *(.bss.*) } > DATA
//   }
//   ENTRY(main);
//
// This test verifies:
//   1. ENTRY(main) correctly sets the entry point (program starts at main)
//   2. .data section (in DATA memory region) is accessible with correct values
//   3. .bss section (in DATA memory region) is functional
//   4. .text section (in CODE memory region) allows function calls

int puts(const char *s);

// ---- Test 1: ENTRY(main) ----
//
// If ENTRY(main) is not handled correctly by the linker, the program
// would either crash at startup or begin execution at the wrong address.
// Reaching the body of main() confirms ENTRY was set correctly.

// ---- Test 2: Data section in DATA region ----
//
// This global variable is placed in .data, which the linker script
// assigns to the DATA memory region (rw, ORIGIN=0x600000).
// Verifies that initialized data in the DATA region is accessible.
int data_value = 42;

// ---- Test 3: BSS section in DATA region ----
//
// This static variable is placed in .bss, which the linker script
// assigns to the DATA memory region. Verifies that zero-initialized
// data is functional.
static int bss_value;

// ---- Test 4: Code in CODE region ----
//
// This function is placed in .text, which the linker script assigns
// to the CODE memory region (rx, ORIGIN=0x400000). Calling this from
// main() verifies that code in the CODE region executes correctly.
int helper(void) {
    return 7;
}

int main(void) {
    // Test 1: We reached main - ENTRY(main) worked.
    // If the linker set the wrong entry point, we would not be here.

    // Test 2: Verify .data section is accessible (placed in DATA region).
    // data_value was initialized to 42 and placed in .data by the compiler.
    // The linker script placed .data into the DATA memory region.
    if (data_value != 42) {
        puts("FAIL: data section value wrong");
        return 1;
    }

    // Test 3: Verify .bss section is functional (placed in DATA region).
    // bss_value is in .bss, which should be zero-initialized by the loader.
    // We write to it and read back to confirm the memory is writable.
    bss_value = 99;
    if (bss_value != 99) {
        puts("FAIL: bss section not writable");
        return 1;
    }

    // Test 4: Verify function call works (code in CODE region).
    // helper() is in .text, placed in the CODE memory region.
    // Calling it verifies the CODE region is executable.
    if (helper() != 7) {
        puts("FAIL: code region function call failed");
        return 1;
    }

    puts("MEMORY and ENTRY OK");
    return 0;
}
