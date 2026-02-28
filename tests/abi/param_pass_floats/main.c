/*
 * ABI integration test: float and double parameter passing via FP registers
 *
 * Verifies that CCC correctly passes float and double function arguments
 * via floating-point registers across all four target architectures:
 *
 *   - x86-64  SysV ABI:  max_float_regs: 8
 *     Float/double args passed in xmm0-xmm7 (CallArgClass::FloatReg).
 *     After 8 FP args, subsequent floats spill to stack.
 *
 *   - AArch64 AAPCS64:   max_float_regs: 8
 *     Float args in s0-s7, double args in d0-d7.
 *     After 8 FP args, spill to stack.
 *
 *   - RISC-V  LP64D:     max_float_regs: 8
 *     Float/double args in fa0-fa7.
 *     After 8 FP args, spill to stack.
 *     (variadic_floats_in_gp: true, but all test functions are non-variadic)
 *
 *   - i686    cdecl:     max_float_regs: 0
 *     ALL float/double arguments go on the stack (CallArgClass::Stack).
 *     The x87 FPU is used for computation but arguments are passed via stack.
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual FP register-based parameter
 * passing ABI mechanism.
 *
 * All floating-point literal values are exactly representable in IEEE 754
 * binary floating point, so exact equality comparison is safe and correct.
 */

#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Test function 1: verify_float
 *
 * Checks that f == 3.25f.
 * Returns 0 on success, 1 on failure.
 *
 * Tests single float argument in first FP register:
 *   x86-64: xmm0 (FP[0])
 *   AArch64: s0 (FP[0])
 *   RISC-V: fa0 (FP[0])
 *   i686: stack
 *
 * 3.25f = 3 + 0.25 = 11.01 in binary = exact IEEE 754 representation.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_float(float f) {
    if (f == 3.25f) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 2: verify_double
 *
 * Checks that d == 7.75.
 * Returns 0 on success, 1 on failure.
 *
 * Tests single double argument in first FP register:
 *   x86-64: xmm0 (FP[0])
 *   AArch64: d0 (FP[0])
 *   RISC-V: fa0 (FP[0])
 *   i686: stack
 *
 * 7.75 = 7 + 0.5 + 0.25 = 111.11 in binary = exact IEEE 754 representation.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_double(double d) {
    if (d == 7.75) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 3: verify_four_floats
 *
 * Checks a == 1.5f, b == 2.5f, c == 3.5f, d == 4.5f.
 * Returns 0 on success, 1 on failure.
 *
 * Tests 4 float arguments occupying FP registers 0-3.
 * All values are exact IEEE 754 representations (sums of powers of two).
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_four_floats(float a, float b, float c, float d) {
    if (a == 1.5f && b == 2.5f && c == 3.5f && d == 4.5f) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 4: verify_four_doubles
 *
 * Checks a == 1.25, b == 2.25, c == 3.25, d == 4.25.
 * Returns 0 on success, 1 on failure.
 *
 * Tests 4 double arguments occupying FP registers 0-3.
 * All values are exact IEEE 754 representations.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_four_doubles(double a, double b, double c, double d) {
    if (a == 1.25 && b == 2.25 && c == 3.25 && d == 4.25) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 5: verify_mixed_fp
 *
 * Checks f1 == 1.5f, d1 == 2.75, f2 == 3.5f, d2 == 4.75.
 * Returns 0 on success, 1 on failure.
 *
 * Tests interleaved float/double arguments sharing the FP register file.
 * Both float and double use the same FP register indices on all architectures.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_mixed_fp(float f1, double d1, float f2, double d2) {
    if (f1 == 1.5f && d1 == 2.75 && f2 == 3.5f && d2 == 4.75) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 6: verify_eight_doubles
 *
 * Checks a == 1.0, b == 2.0, ..., h == 8.0.
 * Returns 0 on success, 1 on failure.
 *
 * Tests maximum FP register occupancy: all 8 FP regs used on 64-bit targets.
 * On i686, all 8 doubles go on the stack (max_float_regs: 0).
 * All values are exact integers representable in IEEE 754.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_eight_doubles(double a, double b, double c, double d,
                         double e, double f, double g, double h) {
    if (a == 1.0 && b == 2.0 && c == 3.0 && d == 4.0 &&
        e == 5.0 && f == 6.0 && g == 7.0 && h == 8.0) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 7: verify_int_float_mix
 *
 * Checks i1 == 42, f1 == 1.5f, i2 == 99, d1 == 2.75.
 * Returns 0 on success, 1 on failure.
 *
 * Tests interleaved integer and FP arguments using independent GP and FP
 * register indices:
 *   x86-64: i1 in rdi (GP[0]), f1 in xmm0 (FP[0]),
 *           i2 in rsi (GP[1]), d1 in xmm1 (FP[1])
 *   AArch64: i1 in x0 (GP[0]), f1 in s0 (FP[0]),
 *            i2 in x1 (GP[1]), d1 in d1 (FP[1])
 *   RISC-V: i1 in a0 (GP[0]), f1 in fa0 (FP[0]),
 *           i2 in a1 (GP[1]), d1 in fa1 (FP[1])
 *   i686: all on stack
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_int_float_mix(int i1, float f1, int i2, double d1) {
    if (i1 == 42 && f1 == 1.5f && i2 == 99 && d1 == 2.75) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 8: sum_floats
 *
 * Returns a + b + c.
 * Tests float return value via FP return register AND that 3 float args
 * arrive correctly.
 *
 * Called with (1.5f, 2.25f, 3.25f), expected result is 7.0f.
 * Arithmetic: 1.5 + 2.25 = 3.75, 3.75 + 3.25 = 7.0 -- exact at every step.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
float sum_floats(float a, float b, float c) {
    return a + b + c;
}

/* ---------------------------------------------------------------------------
 * Test function 9: sum_doubles
 *
 * Returns a + b + c.
 * Tests double return value via FP return register AND that 3 double args
 * arrive correctly.
 *
 * Called with (1.5, 2.25, 3.25), expected result is 7.0.
 * Arithmetic: 1.5 + 2.25 = 3.75, 3.75 + 3.25 = 7.0 -- exact at every step.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
double sum_doubles(double a, double b, double c) {
    return a + b + c;
}

/* ---------------------------------------------------------------------------
 * main -- run all 9 test cases, print results, return failure count
 * --------------------------------------------------------------------------- */
int main(void) {
    int failures = 0;

    /* Test 1: Single float parameter */
    if (verify_float(3.25f) == 0) {
        printf("single_float: OK\n");
    } else {
        printf("single_float: FAIL\n");
        failures++;
    }

    /* Test 2: Single double parameter */
    if (verify_double(7.75) == 0) {
        printf("single_double: OK\n");
    } else {
        printf("single_double: FAIL\n");
        failures++;
    }

    /* Test 3: Four float parameters */
    if (verify_four_floats(1.5f, 2.5f, 3.5f, 4.5f) == 0) {
        printf("four_floats: OK\n");
    } else {
        printf("four_floats: FAIL\n");
        failures++;
    }

    /* Test 4: Four double parameters */
    if (verify_four_doubles(1.25, 2.25, 3.25, 4.25) == 0) {
        printf("four_doubles: OK\n");
    } else {
        printf("four_doubles: FAIL\n");
        failures++;
    }

    /* Test 5: Mixed float/double parameters */
    if (verify_mixed_fp(1.5f, 2.75, 3.5f, 4.75) == 0) {
        printf("mixed_fp: OK\n");
    } else {
        printf("mixed_fp: FAIL\n");
        failures++;
    }

    /* Test 6: Eight doubles (max FP regs) */
    if (verify_eight_doubles(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0) == 0) {
        printf("eight_doubles: OK\n");
    } else {
        printf("eight_doubles: FAIL\n");
        failures++;
    }

    /* Test 7: Integer and float interleaved */
    if (verify_int_float_mix(42, 1.5f, 99, 2.75) == 0) {
        printf("int_float_mix: OK\n");
    } else {
        printf("int_float_mix: FAIL\n");
        failures++;
    }

    /* Test 8: Float sum (return + params) */
    if (sum_floats(1.5f, 2.25f, 3.25f) == 7.0f) {
        printf("sum_floats: OK\n");
    } else {
        printf("sum_floats: FAIL\n");
        failures++;
    }

    /* Test 9: Double sum (return + params) */
    if (sum_doubles(1.5, 2.25, 3.25) == 7.0) {
        printf("sum_doubles: OK\n");
    } else {
        printf("sum_doubles: FAIL\n");
        failures++;
    }

    if (failures == 0) printf("All float param tests passed\n");
    return failures;
}
