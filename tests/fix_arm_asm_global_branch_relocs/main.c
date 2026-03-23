/* Regression test for AArch64 global branch relocation bug fix.
 *
 * Tests that the ARM assembler emits R_AARCH64_CALL26/JUMP26
 * relocations for bl/b instructions targeting global symbols
 * within the same section, rather than resolving the branch
 * offset locally at assembly time.
 *
 * Bug: In elf_writer.rs, resolve_local_branches() resolved
 * same-section branches to global symbols in-place without
 * emitting relocations. GAS always emits relocations for
 * global symbols regardless of whether they are in the same
 * section. The x86 and RISC-V assemblers correctly check for
 * local symbols before resolving.
 *
 * Fix: Add a local-symbol check in resolve_local_branches()
 * so that only .L and .l prefixed local symbols are resolved
 * in-place; global symbols get R_AARCH64_CALL26 or JUMP26
 * relocations for the linker to resolve.
 *
 * Expected output: 42
 * Expected return: 0
 */

int printf(const char *fmt, ...);

/* Global function: target of bl instruction from main().
 *
 * Since this is a global symbol (not static, not .L* prefixed),
 * the assembler must emit an R_AARCH64_CALL26 relocation for
 * any bl instruction targeting this function, even when both
 * caller and callee are in the same .text section.
 *
 * Before the fix, resolve_local_branches() would resolve the
 * bl offset in-place for same-section global symbols, bypassing
 * relocation emission entirely. */
int get_value(void) {
    return 42;
}

int main(void) {
    /* Call get_value() — this generates a 'bl get_value' instruction
     * on AArch64. The assembler must emit R_AARCH64_CALL26 for this
     * bl because get_value is a global symbol in the same section. */
    int result = get_value();
    printf("%d\n", result);
    return 0;
}
