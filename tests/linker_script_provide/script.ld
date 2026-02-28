/* Linker script for testing PROVIDE and KEEP directives.
 *
 * PROVIDE(symbol = expr) defines a symbol only if it is not already
 * defined by any input object file. This is used to supply default
 * values for symbols that programs may optionally override.
 *
 * KEEP(pattern) prevents the linker from garbage-collecting input
 * sections matching the pattern, even when --gc-sections is active.
 */

SECTIONS {
    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
        KEEP(*(.keep_section))
    }

    .bss : {
        *(.bss)
        *(.bss.*)
    }
}

/* PROVIDE: Define provided_magic = 42 only if not defined by object files.
 * The test program does NOT define provided_magic, so the linker should
 * create it with value 42. The C code accesses it as (unsigned long)&provided_magic.
 */
PROVIDE(provided_magic = 42);

/* PROVIDE: Define user_defined = 99, but the test program DOES define
 * user_defined as a global int initialized to 55. Per PROVIDE semantics,
 * this should NOT override the program's definition. The C code verifies
 * that user_defined remains 55.
 */
PROVIDE(user_defined = 99);

/* ENTRY: Set the entry point to main */
ENTRY(main);
