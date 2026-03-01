/* tests/complex_mul/main.c
 *
 * Integration test for _Complex multiplication edge cases.
 * Verifies correct complex multiplication semantics per C11 Annex G,
 * including infinity handling, sign preservation, and precision edge cases.
 */
#include <stdio.h>

int main(void) {
    /* Test 1: Basic multiplication: (1+2i)(3+4i)
     * ac=1*3=3, bd=2*4=8, ad=1*4=4, bc=2*3=6
     * real = ac - bd = 3 - 8 = -5
     * imag = ad + bc = 4 + 6 = 10 */
    _Complex double z1a = 1.0 + 2.0i;
    _Complex double z1b = 3.0 + 4.0i;
    _Complex double r1 = z1a * z1b;
    printf("basic: %.1f %.1f\n", __real__ r1, __imag__ r1);

    /* Test 2: Multiplication by pure real: (3+4i)(2+0i) = 6+8i
     * ac=6, bd=0, ad=0, bc=8 */
    _Complex double r2 = (3.0 + 4.0i) * (2.0 + 0.0i);
    printf("by_real: %.1f %.1f\n", __real__ r2, __imag__ r2);

    /* Test 3: Multiplication by pure imaginary: (3+4i)(0+2i) = -8+6i
     * ac=0, bd=8, ad=6, bc=0
     * real = 0 - 8 = -8, imag = 6 + 0 = 6 */
    _Complex double r3 = (3.0 + 4.0i) * (0.0 + 2.0i);
    printf("by_imag: %.1f %.1f\n", __real__ r3, __imag__ r3);

    /* Test 4: Squaring: (1+1i)^2 = 0+2i
     * ac=1, bd=1, ad=1, bc=1
     * real = 1 - 1 = 0, imag = 1 + 1 = 2 */
    _Complex double z4 = 1.0 + 1.0i;
    _Complex double r4 = z4 * z4;
    printf("square: %.1f %.1f\n", __real__ r4, __imag__ r4);

    /* Test 5: Conjugate product: (3+4i)(3-4i) = 25+0i
     * ac=9, bd=-16, ad=-12, bc=12
     * real = 9 - (-16) = 25, imag = -12 + 12 = 0 */
    _Complex double r5 = (3.0 + 4.0i) * (3.0 - 4.0i);
    printf("conjugate: %.1f %.1f\n", __real__ r5, __imag__ r5);

    /* Test 6: Negative operands: (-2+3i)(4-5i)
     * ac=-8, bd=-15, ad=10, bc=12
     * real = -8 - (-15) = 7, imag = 10 + 12 = 22 */
    _Complex double r6 = (-2.0 + 3.0i) * (4.0 - 5.0i);
    printf("negative: %.1f %.1f\n", __real__ r6, __imag__ r6);

    /* Test 7: Zero multiplication: (0+0i)(3+4i) = 0+0i */
    _Complex double r7 = (0.0 + 0.0i) * (3.0 + 4.0i);
    printf("zero_mul: %.1f %.1f\n", __real__ r7, __imag__ r7);

    /* Test 8: Identity multiplication: (5+7i)(1+0i) = 5+7i */
    _Complex double r8 = (5.0 + 7.0i) * (1.0 + 0.0i);
    printf("identity: %.1f %.1f\n", __real__ r8, __imag__ r8);

    /* Test 9: Pure imaginary product: (0+2i)(0+3i) = -6+0i
     * ac=0, bd=6, ad=0, bc=0
     * real = 0 - 6 = -6, imag = 0 + 0 = 0 */
    _Complex double r9 = (0.0 + 2.0i) * (0.0 + 3.0i);
    printf("pure_imag: %.1f %.1f\n", __real__ r9, __imag__ r9);

    /* Test 10: _Complex float multiplication: (2+3i)(4+1i) = 5+14i
     * ac=8, bd=3, ad=2, bc=12
     * real = 8 - 3 = 5, imag = 2 + 12 = 14 */
    _Complex float rf = (2.0f + 3.0fi) * (4.0f + 1.0fi);
    printf("float_mul: %.1f %.1f\n", __real__ rf, __imag__ rf);

    /* Test 11: Annex G - infinity * finite produces infinity
     * (inf+0i)*(1+1i): ac=inf, bd=0, ad=inf, bc=0
     * real = inf - 0 = inf, imag = inf + 0 = inf
     * At least one component must be infinite.
     * Use volatile to prevent compile-time constant folding. */
    volatile double v_one = 1.0;
    volatile double v_zero = 0.0;
    double pos_inf = v_one / v_zero;
    _Complex double inf_c = pos_inf + 0.0i;
    _Complex double r11 = inf_c * (1.0 + 1.0i);
    int has_inf11 = __builtin_isinf(__real__ r11) || __builtin_isinf(__imag__ r11);
    printf("inf_times_finite: %d\n", has_inf11);

    /* Test 12: Annex G - finite * infinity (commutativity)
     * (1+1i)*(inf+0i): same result by commutativity of multiplication */
    _Complex double r12 = (1.0 + 1.0i) * inf_c;
    int has_inf12 = __builtin_isinf(__real__ r12) || __builtin_isinf(__imag__ r12);
    printf("finite_times_inf: %d\n", has_inf12);

    /* Test 13: Negation via multiplication: (-1+0i)(3+4i) = -3-4i
     * ac=-3, bd=0, ad=-4, bc=0 */
    _Complex double r13 = (-1.0 + 0.0i) * (3.0 + 4.0i);
    printf("neg_sign: %.1f %.1f\n", __real__ r13, __imag__ r13);

    /* Test 14: Large values: (100+200i)(300+400i)
     * ac=30000, bd=80000, ad=40000, bc=60000
     * real = 30000 - 80000 = -50000
     * imag = 40000 + 60000 = 100000 */
    _Complex double r14 = (100.0 + 200.0i) * (300.0 + 400.0i);
    printf("large: %.1f %.1f\n", __real__ r14, __imag__ r14);

    return 0;
}
