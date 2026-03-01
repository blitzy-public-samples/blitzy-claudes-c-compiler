/* tests/complex_div/main.c
 *
 * Integration test for _Complex division edge cases.
 * Verifies correct complex division semantics per C11 Annex G,
 * including division by zero, infinity, and precision edge cases.
 *
 * Each Annex G edge-case test uses a separate function to avoid
 * stack frame size interactions between volatile/infinity operations
 * and normal division code paths.
 */
#include <stdio.h>

/* Basic double-precision complex division tests */
static void test_basic_division(void) {
    /* Test 1: Basic division: (2+4i) / (1+1i) = (3+1i)
     * ac+bd = 2+4 = 6, bc-ad = 4-2 = 2, c^2+d^2 = 2
     * Result: (6/2, 2/2) = (3, 1) */
    _Complex double z1a = 2.0 + 4.0i;
    _Complex double z1b = 1.0 + 1.0i;
    _Complex double r1 = z1a / z1b;
    printf("basic: %.1f %.1f\n", __real__ r1, __imag__ r1);

    /* Test 2: Division by pure real: (8+6i) / (2+0i) = (4+3i)
     * ac+bd = 16, bc-ad = 12, denom = 4 */
    _Complex double r2 = (8.0 + 6.0i) / (2.0 + 0.0i);
    printf("by_real: %.1f %.1f\n", __real__ r2, __imag__ r2);

    /* Test 3: Division by pure imaginary: (4+6i) / (0+2i) = (3-2i)
     * ac+bd = 0+12 = 12, bc-ad = 0-8 = -8, denom = 4 */
    _Complex double r3 = (4.0 + 6.0i) / (0.0 + 2.0i);
    printf("by_imag: %.1f %.1f\n", __real__ r3, __imag__ r3);

    /* Test 4: Self-division: (5+12i) / (5+12i) = (1+0i)
     * ac+bd = 25+144 = 169, bc-ad = 60-60 = 0, denom = 169 */
    _Complex double z4 = 5.0 + 12.0i;
    _Complex double r4 = z4 / z4;
    printf("self_div: %.1f %.1f\n", __real__ r4, __imag__ r4);

    /* Test 5: Real divided by imaginary: (1+0i) / (0+1i) = (0-1i)
     * ac+bd = 0, bc-ad = 0-1 = -1, denom = 1 */
    _Complex double r5 = (1.0 + 0.0i) / (0.0 + 1.0i);
    printf("real_by_imag: %.1f %.1f\n", __real__ r5, __imag__ r5);

    /* Test 6: Negative dividend: (-6-8i) / (2+0i) = (-3-4i)
     * ac+bd = -12, bc-ad = -16, denom = 4 */
    _Complex double r6 = (-6.0 - 8.0i) / (2.0 + 0.0i);
    printf("neg_div: %.1f %.1f\n", __real__ r6, __imag__ r6);

    /* Test 7: Non-integer result: (3+4i) / (1+2i) = (2.2-0.4i)
     * ac+bd = 3+8 = 11, bc-ad = 4-6 = -2, denom = 5
     * Result: (11/5, -2/5) = (2.2, -0.4) */
    _Complex double r7 = (3.0 + 4.0i) / (1.0 + 2.0i);
    printf("general: %.1f %.1f\n", __real__ r7, __imag__ r7);

    /* Test 8: Zero divided by nonzero: (0+0i) / (3+4i) = (0+0i)
     * Numerators are both 0, denom = 25 */
    _Complex double r8 = (0.0 + 0.0i) / (3.0 + 4.0i);
    printf("zero_div: %.1f %.1f\n", __real__ r8, __imag__ r8);
}

/* Test 9: Annex G - nonzero / zero -> infinity (G.5.1 para 6)
 * Naive formula gives NaN; Annex G recovery produces infinity.
 * Use volatile to ensure runtime evaluation. */
static void test_div_by_zero(void) {
    volatile double v_zero = 0.0;
    _Complex double z9 = 1.0 + 2.0i;
    _Complex double zero9 = v_zero + v_zero * 1.0i;
    _Complex double r9 = z9 / zero9;
    int has_inf9 = __builtin_isinf(__real__ r9) || __builtin_isinf(__imag__ r9);
    printf("div_by_zero: %d\n", has_inf9);
}

/* Test 10: Annex G - finite / infinity -> zero (G.5.1 para 6)
 * When divisor has infinite component and dividend is finite,
 * the result must be zero. */
static void test_finite_div_inf(void) {
    volatile double v_zero = 0.0;
    volatile double v_one = 1.0;
    double pos_inf = v_one / v_zero;
    _Complex double finite10 = 1.0 + 2.0i;
    _Complex double inf_c10 = pos_inf + 0.0i;
    _Complex double r10 = finite10 / inf_c10;
    printf("finite_div_inf: %.1f %.1f\n", __real__ r10, __imag__ r10);
}

/* Test 11: Annex G - infinity / finite -> infinity (G.5.1 para 6)
 * The naive formula handles this correctly (inf/5 = inf). */
static void test_inf_div_finite(void) {
    volatile double v_zero = 0.0;
    volatile double v_one = 1.0;
    double pos_inf = v_one / v_zero;
    _Complex double inf_c11 = pos_inf + 0.0i;
    _Complex double finite11 = 1.0 + 2.0i;
    _Complex double r11 = inf_c11 / finite11;
    int has_inf11 = __builtin_isinf(__real__ r11) || __builtin_isinf(__imag__ r11);
    printf("inf_div_finite: %d\n", has_inf11);
}

/* Test 12: Conjugate division: (1+2i) / (1-2i) = (-0.6+0.8i) */
static void test_conjugate_division(void) {
    _Complex double r12 = (1.0 + 2.0i) / (1.0 - 2.0i);
    printf("conjugate: %.1f %.1f\n", __real__ r12, __imag__ r12);
}

/* Test 13: _Complex float division: (6+8fi) / (2+0fi) = (3+4fi)
 * Verifies float-precision complex division in its own function
 * to avoid register allocation interactions with F64 branch code. */
static void test_float_division(void) {
    _Complex float rf = (6.0f + 8.0fi) / (2.0f + 0.0fi);
    printf("float_div: %.1f %.1f\n", __real__ rf, __imag__ rf);
}

/* Test 14: Small denominator precision: (1+1i) / (0.5+0.5i) = (2+0i) */
static void test_small_denom_division(void) {
    _Complex double r14 = (1.0 + 1.0i) / (0.5 + 0.5i);
    printf("small_denom: %.1f %.1f\n", __real__ r14, __imag__ r14);
}

int main(void) {
    test_basic_division();
    test_div_by_zero();
    test_finite_div_inf();
    test_inf_div_finite();
    test_conjugate_division();
    test_float_division();
    test_small_denom_division();
    return 0;
}
