/* Linker script for testing MEMORY and ENTRY directives.
 *
 * MEMORY { } defines named memory regions with origin addresses,
 * lengths, and access attributes (r=read, w=write, x=execute).
 * The linker verifies that output sections placed into a region
 * fit within the region's ORIGIN + LENGTH bounds.
 *
 * ENTRY(symbol) sets the ELF entry point to the specified symbol,
 * which determines where execution begins when the OS loads the
 * binary.
 *
 * The > REGION syntax in SECTIONS places output sections into
 * named memory regions defined in the MEMORY block.
 */

MEMORY {
    /* CODE region: read + execute, for .text and .rodata sections.
     * Starting at 0x400000 with 1 MiB of space. */
    CODE (rx)  : ORIGIN = 0x400000, LENGTH = 0x100000

    /* DATA region: read + write, for .data and .bss sections.
     * Starting at 0x600000 with 1 MiB of space. */
    DATA (rw)  : ORIGIN = 0x600000, LENGTH = 0x100000
}

SECTIONS {
    /* Place executable code into the CODE memory region */
    .text : {
        *(.text)
        *(.text.*)
    } > CODE

    /* Place read-only data into the CODE memory region */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > CODE

    /* Place initialized data into the DATA memory region */
    .data : {
        *(.data)
        *(.data.*)
    } > DATA

    /* Place zero-initialized data into the DATA memory region */
    .bss : {
        *(.bss)
        *(.bss.*)
    } > DATA
}

/* Set the ELF entry point to the main function */
ENTRY(main);
