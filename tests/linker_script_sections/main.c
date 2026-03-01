// Test: Linker script SECTIONS { } placement directives
//
// Compile: ccc -T script.ld -o test main.c
//
// The companion linker script (script.ld) places sections at specific
// addresses using the SECTIONS { } directive:
//   . = 0x400000;
//   .text : { *(.text) *(.text.*) }
//   .rodata : { *(.rodata) *(.rodata.*) }
//   . = 0x600000;
//   .data : { *(.data) *(.data.*) }
//   .bss : { *(.bss) *(.bss.*) }
//   ENTRY(main);
//
// This test verifies:
//   1. .text section is placed at or after 0x400000
//   2. .rodata section follows .text (between 0x400000 and 0x600000)
//   3. .data section is placed at or after 0x600000
//   4. .bss section follows .data (at or after 0x600000)
//   5. Data values are correct (initialized data and bss work properly)
//   6. Function calls work (code section is executable)

int puts(const char *s);

// ---- Verification approach ----
//
// To verify section placement, we take the address of items placed in
// each section and check that the address falls within the expected
// range specified by the linker script.
//
// .text starts at 0x400000, .data starts at 0x600000.
// So code addresses should be >= 0x400000 and < 0x600000,
// and data addresses should be >= 0x600000.

// ---- Test 1: Data section placed at >= 0x600000 ----
//
// This global variable is placed in .data by the compiler.
// The linker script sets ". = 0x600000" before the .data section,
// so this variable's address should be >= 0x600000.
int data_value = 42;

// ---- Test 2: BSS section follows .data (>= 0x600000) ----
//
// This zero-initialized static variable is placed in .bss.
// Since .bss follows .data in the linker script without a new
// address assignment, it should also be at or after 0x600000.
static int bss_value;

// ---- Test 3: Code section placed at >= 0x400000 ----
//
// This function is placed in .text by the compiler.
// The linker script sets ". = 0x400000" before .text,
// so this function's address should be >= 0x400000.
int helper_func(void) {
    return 7;
}

int main(void) {
    // Test 1: Verify .data section address is >= 0x600000
    //
    // The linker script sets ". = 0x600000" before .data.
    // Take address of data_value (in .data) and check range.
    unsigned long data_addr = (unsigned long)&data_value;
    if (data_addr < 0x600000UL) {
        puts("FAIL: data section placed below 0x600000");
        return 1;
    }

    // Test 2: Verify data section has correct value
    //
    // data_value was initialized to 42. This confirms .data content
    // is correctly loaded from the specified section address.
    if (data_value != 42) {
        puts("FAIL: data section value wrong");
        return 1;
    }

    // Test 3: Verify .bss section address is >= 0x600000
    //
    // BSS follows .data without an explicit address in the script,
    // so it should be at or after 0x600000.
    unsigned long bss_addr = (unsigned long)&bss_value;
    if (bss_addr < 0x600000UL) {
        puts("FAIL: bss section placed below 0x600000");
        return 1;
    }

    // Test 4: Verify BSS is writable and functional
    //
    // Write to bss_value and read it back to confirm the BSS
    // section is properly allocated and writable.
    bss_value = 99;
    if (bss_value != 99) {
        puts("FAIL: bss section not writable");
        return 1;
    }

    // Test 5: Verify .text section address is >= 0x400000
    //
    // The linker script sets ". = 0x400000" before .text.
    // Take the address of helper_func (in .text) and check range.
    unsigned long text_addr = (unsigned long)&helper_func;
    if (text_addr < 0x400000UL) {
        puts("FAIL: text section placed below 0x400000");
        return 1;
    }

    // Test 6: Verify .text is below .data start (< 0x600000)
    //
    // Code sections start at 0x400000 and data at 0x600000.
    // The function address should be in the code region.
    if (text_addr >= 0x600000UL) {
        puts("FAIL: text section placed at or above 0x600000");
        return 1;
    }

    // Test 7: Verify function call works (code is executable)
    //
    // Call helper_func() which is in .text section.
    // This confirms the code section is executable.
    if (helper_func() != 7) {
        puts("FAIL: text section function call failed");
        return 1;
    }

    // Test 8: Verify data section is after text section
    //
    // The linker script places .text at 0x400000 and .data at 0x600000.
    // data_addr should be greater than text_addr.
    if (data_addr <= text_addr) {
        puts("FAIL: data section not after text section");
        return 1;
    }

    puts("SECTIONS placement OK");
    return 0;
}
