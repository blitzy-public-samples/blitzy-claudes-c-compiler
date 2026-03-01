/* Regression test for AArch64 MOVW symbolic relocation bug fix.
 *
 * Tests that the ARM assembler correctly emits MOVW relocation types
 * (MovwUabsG0Nc, MovwUabsG1Nc, MovwUabsG2Nc, MovwUabsG3) when
 * movz/movk instructions use :abs_g*: modifiers with symbolic references.
 *
 * Before the fix, resolve_abs_g_modifier() returning None for symbols
 * caused a fallthrough to get_imm() which failed with "expected immediate
 * at operand". After the fix, the encoder emits WordWithReloc entries
 * with the appropriate MOVW relocation types, allowing the linker to
 * resolve the symbol address at link time.
 *
 * This pattern is used in the Linux kernel's tramp_alias macro:
 *   .set .Lalias, offset + \sym - .entry.tramp.text
 *   movz \dst, :abs_g2_s:.Lalias
 *   movk \dst, :abs_g1_nc:.Lalias
 *   movk \dst, :abs_g0_nc:.Lalias
 *
 * Expected output: 42
 * Expected return: 0
 */

int printf(const char *fmt, ...);

int global_val = 42;

int main(void) {
    long addr;

    /* Load the absolute address of global_val using movz + movk with
     * unsigned absolute :abs_g*: modifiers. Each instruction loads
     * 16 bits of the 64-bit address. The assembler must emit MOVW
     * relocations (not fail with "expected immediate"):
     *   movz -> R_AARCH64_MOVW_UABS_G3
     *   movk -> R_AARCH64_MOVW_UABS_G2_NC
     *   movk -> R_AARCH64_MOVW_UABS_G1_NC
     *   movk -> R_AARCH64_MOVW_UABS_G0_NC */
    __asm__ volatile (
        "movz %0, #:abs_g3:global_val\n\t"
        "movk %0, #:abs_g2_nc:global_val\n\t"
        "movk %0, #:abs_g1_nc:global_val\n\t"
        "movk %0, #:abs_g0_nc:global_val\n\t"
        : "=r" (addr)
    );

    /* Dereference the address loaded via MOVW relocations and verify
     * it points to global_val with value 42 */
    printf("%d\n", *(int *)addr);

    return 0;
}
