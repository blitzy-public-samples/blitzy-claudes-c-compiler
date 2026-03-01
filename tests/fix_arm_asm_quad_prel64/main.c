/* Regression test for AArch64 .quad PREL64 relocation bug fix.
 *
 * Tests that the ARM assembler correctly handles .quad with
 * symbol+offset-. patterns:
 *
 *   1. Parser: decomposes "values+12" into symbol "values"
 *      and numeric addend 12 (not as the literal symbol "values+12")
 *   2. ELF writer: emits R_AARCH64_PREL64 (type 260) for .quad
 *      (8-byte) data, not R_AARCH64_PREL32 (type 261) which truncates
 *   3. Encoder: has the Prel64 relocation variant for 64-bit
 *      PC-relative references
 *
 * This pattern is used in the Linux kernel's __jump_table section:
 *   .quad cgroup_bpf_enabled_key+48 - .
 *
 * Before the fix:
 *   - undefined reference to `values+12' (parser: composite name)
 *   - relocation truncated to fit: R_AARCH64_PREL32 (wrong type)
 *
 * Expected output: 42
 * Expected return: 0
 */

int printf(const char *fmt, ...);

int values[4] = {10, 20, 30, 42};

int main(void) {
    long ptr;
    long base;

    /* Exercise .quad with symbol+offset - label pattern.
     *
     * The assembler must:
     *   1. Parse "values+12" as symbol="values", addend=12
     *   2. Emit R_AARCH64_PREL64 (not PREL32) for the .quad
     *   3. The linker resolves: S + A - P = values + 12 - quad_addr
     *
     * At runtime:
     *   base (%1) = address of label 1 = address of .quad data
     *   ptr  (%0) = .quad value = (values+12) - base
     *   ptr + base = values + 12 = absolute address of values[3]
     *   *(int*)ptr = 42
     */
    __asm__ volatile(
        "adr %1, 1f\n\t"
        "ldr %0, [%1]\n\t"
        "add %0, %0, %1\n\t"
        "b 2f\n\t"
        ".align 3\n\t"
        "1:\n\t"
        ".quad values+12 - 1b\n\t"
        "2:\n\t"
        : "=&r"(ptr), "=&r"(base)
    );

    printf("%d\n", *(int *)ptr);
    return 0;
}
