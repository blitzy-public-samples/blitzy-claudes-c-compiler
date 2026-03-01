/* Regression test for AArch64 .org directive bug fix.
 *
 * Tests that the ARM assembler correctly implements the .org directive
 * with zero-padding semantics. The .org directive should pad the current
 * section position up to the specified offset by inserting zero bytes
 * (matching GAS behavior).
 *
 * This is critical for the Linux kernel's vector table, which uses:
 *   .org .Lventry_start + 128
 * to pad each vector entry to exactly 128 bytes.
 *
 * Before the fix, .org was treated as AsmDirective::Ignored (parser.rs
 * lines 1672-1676), causing vector table entries to be misaligned and
 * all branch targets to be wrong, making the ARM kernel fail to boot.
 *
 * Expected output: 42
 * Expected return: 0
 */

int printf(const char *fmt, ...);

int main(void) {
    long start;
    long end;

    /* Place a data block with .org padding:
     *   offset 0:  .word 0           (placeholder data)
     *   .org 0b + 32                 (pad to offset 32 from start with zeros)
     *   offset 32: .word 42          (value to verify)
     *
     * Without the .org fix, the .word 42 would be placed at offset 4
     * (immediately after .word 0), not at offset 32. We verify .org works
     * by reading at offset 32 and expecting 42.
     */
    __asm__ volatile(
        "b 2f\n\t"
        ".align 3\n\t"
        "0:\n\t"
        ".word 0\n\t"
        ".org 0b + 32\n\t"
        ".word 42\n\t"
        "1:\n\t"
        "2:\n\t"
        "adr %0, 0b\n\t"
        "adr %1, 1b\n\t"
        : "=r"(start), "=r"(end)
    );

    /* Read the .word at offset 32 from the start label.
     * If .org worked correctly, this is 42.
     * If .org was ignored, this would be whatever is at offset 32
     * (likely 0 or garbage), while 42 would be at offset 4 instead.
     */
    int val = *(int *)(start + 32);
    printf("%d\n", val);
    return 0;
}
