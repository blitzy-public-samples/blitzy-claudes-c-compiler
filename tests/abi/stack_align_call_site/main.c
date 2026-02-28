/*
 * tests/abi/stack_align_call_site/main.c
 *
 * ABI stack alignment integration test for CCC.
 *
 * Verifies that the CCC compiler maintains proper stack alignment at call
 * sites, even when an odd number of arguments spill to the stack.
 *
 * Key ABI alignment requirements:
 *   - x86-64 SysV ABI:   16-byte stack alignment before 'call' instruction.
 *                         6 GP regs (rdi, rsi, rdx, rcx, r8, r9), 8 XMM regs.
 *   - AArch64 AAPCS64:   16-byte stack alignment at call.
 *                         8 GP regs (x0-x7), 8 FP regs (d0-d7).
 *   - RISC-V LP64D:      16-byte stack alignment at call.
 *                         8 GP regs (a0-a7), 8 FP regs (fa0-fa7).
 *   - i686 cdecl:        ALL args on stack; 16-byte alignment on modern Linux.
 *
 * When the number of stack-pushed bytes is not a multiple of 16, the
 * compiler must insert alignment padding. Misalignment causes stack
 * arguments to be read from incorrect offsets, corrupting parameter values.
 *
 * The primary verification mechanism is correct argument passing across the
 * call boundary: each callee function sums its arguments and returns the
 * result. If alignment is wrong, stack arguments will be read from incorrect
 * offsets, and the sum will be wrong.
 */

#include <stdio.h>

/* =========================================================================
 * Callee functions that receive stack-passed arguments
 * ========================================================================= */

/*
 * sum_7_ints: 7 integer arguments.
 *
 * On x86-64:     6 args in GP registers, 1 on stack (8 bytes).
 *                8 bytes is not a multiple of 16, so the compiler MUST
 *                add 8 bytes of padding to maintain 16-byte alignment.
 *                This is the PRIMARY alignment test for x86-64.
 *
 * On AArch64:    All 7 fit in GP registers (8 available), no stack args.
 * On RISC-V:     All 7 fit in GP registers (8 available), no stack args.
 * On i686:       All 7 on stack (28 bytes), padded to 32 bytes.
 */
int sum_7_ints(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

/*
 * sum_9_ints: 9 integer arguments.
 *
 * On x86-64:     6 in GP registers, 3 on stack (24 bytes).
 *                24 bytes is not a multiple of 16, needs 8-byte padding to 32.
 *
 * On AArch64:    8 in GP registers, 1 on stack (8 bytes).
 *                8 bytes is not a multiple of 16, needs padding.
 *                This is the PRIMARY alignment test for AArch64.
 *
 * On RISC-V:     8 in GP registers, 1 on stack (8 bytes), needs padding.
 *                This is the PRIMARY alignment test for RISC-V.
 *
 * On i686:       All 9 on stack (36 bytes), padded to 48 bytes.
 */
int sum_9_ints(int a, int b, int c, int d, int e, int f, int g, int h, int i) {
    return a + b + c + d + e + f + g + h + i;
}

/*
 * sum_8_ints: 8 integer arguments.
 *
 * On x86-64:     6 in GP registers, 2 on stack (16 bytes).
 *                16 bytes IS a multiple of 16, naturally aligned.
 *                Baseline test: no extra padding should be needed on x86-64.
 *
 * On AArch64:    All 8 fit in GP registers, no stack args.
 * On RISC-V:     All 8 fit in GP registers, no stack args.
 * On i686:       All 8 on stack (32 bytes), naturally aligned to 16.
 */
int sum_8_ints(int a, int b, int c, int d, int e, int f, int g, int h) {
    return a + b + c + d + e + f + g + h;
}

/*
 * sum_mixed_spill: 7 int args + 9 double args.
 *
 * Exercises both GP and FP register class overflow simultaneously.
 *
 * On x86-64:     6 ints in GP regs, 1 int on stack (8 bytes);
 *                8 doubles in XMM regs, 1 double on stack (8 bytes).
 *                Total stack: 8 + 8 = 16 bytes (naturally aligned).
 *
 * On AArch64:    All 7 ints in GP regs (8 available);
 *                8 doubles in FP regs, 1 double on stack.
 *
 * On RISC-V:     All 7 ints in GP regs (8 available);
 *                8 doubles in FP regs, 1 double on stack.
 *
 * On i686:       All args on stack.
 *
 * Returns integer result for determinism: 28 + 45 = 73.
 */
int sum_mixed_spill(int i1, int i2, int i3, int i4, int i5, int i6, int i7,
                    double d1, double d2, double d3, double d4,
                    double d5, double d6, double d7, double d8, double d9) {
    int int_sum = i1 + i2 + i3 + i4 + i5 + i6 + i7;
    double dbl_sum = d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8 + d9;
    return int_sum + (int)dbl_sum;
}

/*
 * probe_alignment_7: Stack alignment probe with 7 int args.
 *
 * Takes 7 integer arguments (odd stack count on x86-64), then inspects the
 * address of a local variable to verify the stack frame was set up with
 * proper alignment.
 *
 * Returns 1 if:
 *   - The local variable address is at least 4-byte aligned (int alignment)
 *   - All 7 arguments arrived correctly (sum == 28)
 * Returns 0 if either check fails, indicating stack misalignment or
 * argument corruption.
 *
 * The volatile qualifier prevents the compiler from optimizing away the
 * local variable, ensuring it occupies actual stack space.
 */
int probe_alignment_7(int a, int b, int c, int d, int e, int f, int g) {
    volatile int local_probe;
    local_probe = a + g;  /* Use volatile to prevent optimization away */
    unsigned long addr = (unsigned long)&local_probe;
    /* Check arguments arrived correctly */
    int sum = a + b + c + d + e + f + g;
    /* If stack was properly aligned at call site, the frame should be well-formed.
       The local variable should be at least 4-byte aligned (int alignment).
       We verify 4-byte alignment as a minimum sanity check. */
    int aligned = (addr % 4 == 0) ? 1 : 0;
    if (sum != 28) return 0;  /* Arguments corrupted = misalignment */
    return aligned;
}

/* =========================================================================
 * Main — run all stack alignment tests
 * ========================================================================= */

int main(void) {
    int failures = 0;
    int result;

    /* Test 1: 7 int args — odd stack args on x86-64 */
    result = sum_7_ints(1, 2, 3, 4, 5, 6, 7);
    if (result == 28) {
        printf("7_int_args: OK\n");
    } else {
        printf("7_int_args: FAIL (got %d, expected 28)\n", result);
        failures++;
    }

    /* Test 2: 9 int args — odd stack args on all architectures */
    result = sum_9_ints(1, 2, 3, 4, 5, 6, 7, 8, 9);
    if (result == 45) {
        printf("9_int_args: OK\n");
    } else {
        printf("9_int_args: FAIL (got %d, expected 45)\n", result);
        failures++;
    }

    /* Test 3: 8 int args — even stack args (naturally aligned on x86-64) */
    result = sum_8_ints(1, 2, 3, 4, 5, 6, 7, 8);
    if (result == 36) {
        printf("8_int_args: OK\n");
    } else {
        printf("8_int_args: FAIL (got %d, expected 36)\n", result);
        failures++;
    }

    /* Test 4: Mixed int + double with both register classes spilling */
    result = sum_mixed_spill(1, 2, 3, 4, 5, 6, 7,
                             1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);
    if (result == 73) {
        printf("mixed_spill: OK\n");
    } else {
        printf("mixed_spill: FAIL (got %d, expected 73)\n", result);
        failures++;
    }

    /* Test 5: Alignment probe — verifies stack frame correctness */
    result = probe_alignment_7(1, 2, 3, 4, 5, 6, 7);
    if (result == 1) {
        printf("align_probe: OK\n");
    } else {
        printf("align_probe: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All stack alignment tests passed\n");
    }

    return failures;
}
