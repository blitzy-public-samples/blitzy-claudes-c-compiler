/* Linker script for testing SECTIONS { } placement directives.
 *
 * This script exercises:
 *   1. SECTIONS { } block with explicit output section definitions
 *   2. Wildcard input section matching: *(.text), *(.text.*), etc.
 *   3. Address assignment using ". = <address>" to place sections
 *      at specific virtual addresses
 *   4. Standard output sections: .text, .rodata, .data, .bss
 *
 * The test program (main.c) verifies that sections are placed
 * at the addresses specified in this script by taking the address
 * of variables/functions in each section and checking address ranges.
 */

SECTIONS {
    /* Place the .text section starting at 0x400000.
     * This is the standard text segment base on x86-64 Linux and
     * works on all four CCC target architectures (x86-64, AArch64,
     * RISC-V 64, i686) since all support addresses >= 0x400000.
     */
    . = 0x400000;
    .text : {
        *(.text)
        *(.text.*)
    }

    /* Place read-only data immediately after .text.
     * The linker advances the location counter automatically.
     */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Place initialized data at 0x600000.
     * Explicit address assignment creates a gap between code/rodata
     * and writable data, which is the standard memory layout.
     */
    . = 0x600000;
    .data : {
        *(.data)
        *(.data.*)
    }

    /* Place zero-initialized data immediately after .data.
     * BSS follows data without an explicit address, testing
     * that the linker advances the location counter from .data.
     */
    .bss : {
        *(.bss)
        *(.bss.*)
    }
}

/* Set the ELF entry point to main */
ENTRY(main);
