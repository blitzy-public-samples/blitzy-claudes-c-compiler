// Test: linker script SECTIONS { } placement directives
//
// Compile: ccc -T script.ld -o test main.c
//
// This test verifies that the linker processes SECTIONS { } directives
// in script.ld and places output sections at the specified addresses:
//   .text   at 0x400000
//   .data   at 0x600000
//
// The program takes its own function address and a global variable address,
// then checks that they fall within the expected address ranges.

int printf(const char *fmt, ...);

// A global variable in .data section
int global_data = 42;

int main(void) {
    // Get the address of main (which is in .text)
    unsigned long text_addr = (unsigned long)&main;

    // Get the address of global_data (which is in .data)
    unsigned long data_addr = (unsigned long)&global_data;

    // Verify .text section is at or above 0x400000
    // and .data section is at or above 0x600000
    // Use range checks: addr should be in [base, base + 0x100000)
    int text_ok = (text_addr >= 0x400000UL && text_addr < 0x500000UL);
    int data_ok = (data_addr >= 0x600000UL && data_addr < 0x700000UL);

    if (text_ok && data_ok) {
        printf("SECTIONS placement OK\n");
    } else {
        if (!text_ok)
            printf("FAIL: .text at 0x%lx, expected >= 0x400000\n", text_addr);
        if (!data_ok)
            printf("FAIL: .data at 0x%lx, expected >= 0x600000\n", data_addr);
    }

    return 0;
}
