// Test: linker script MEMORY { } and ENTRY() directives
//
// Compile: ccc -T script.ld -o test main.c
//
// This test verifies that the linker processes:
//   MEMORY { CODE (rx): ORIGIN=0x400000, LENGTH=0x100000;
//            DATA (rw): ORIGIN=0x600000, LENGTH=0x100000 }
//   ENTRY(main)
//
// The test checks that .text falls in CODE region and .data in DATA region.

int printf(const char *fmt, ...);

// Global variable in .data section (placed in DATA memory region)
int memory_test_var = 99;

int main(void) {
    // Get address of main (should be in CODE region: 0x400000..0x4FFFFF)
    unsigned long code_addr = (unsigned long)&main;

    // Get address of global (should be in DATA region: 0x600000..0x6FFFFF)
    unsigned long data_addr = (unsigned long)&memory_test_var;

    int code_ok = (code_addr >= 0x400000UL && code_addr < 0x500000UL);
    int data_ok = (data_addr >= 0x600000UL && data_addr < 0x700000UL);

    if (code_ok && data_ok) {
        printf("MEMORY and ENTRY OK\n");
    } else {
        if (!code_ok)
            printf("FAIL: code at 0x%lx, expected in CODE region\n", code_addr);
        if (!data_ok)
            printf("FAIL: data at 0x%lx, expected in DATA region\n", data_addr);
    }

    return 0;
}
