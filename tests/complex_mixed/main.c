/* tests/complex_mixed/main.c
 *
 * Integration test for mixed real/_Complex arithmetic operations.
 * Verifies type promotion, implicit conversions, and result computation
 * per C11 §6.3.1.7 (Real and complex) and §6.3.1.8 (Usual arithmetic
 * conversions).
 */
#include <stdio.h>

int main(void) {
    /* Test 1: double + _Complex double → _Complex double
     * Real 3.0 promoted to (3.0+0.0i), then added component-wise. */
    _Complex double z1 = 1.0 + 2.0i;
    double r1 = 3.0;
    _Complex double res1 = r1 + z1;
    printf("real+complex: %.1f %.1f\n", __real__ res1, __imag__ res1);

    /* Test 2: _Complex double + double → _Complex double (commutativity) */
    _Complex double res2 = z1 + r1;
    printf("complex+real: %.1f %.1f\n", __real__ res2, __imag__ res2);

    /* Test 3: double * _Complex double → _Complex double
     * 2.0 promoted to (2.0+0.0i), multiplication: (2*3-0*4)+(2*4+0*3)i = 6+8i */
    double r3 = 2.0;
    _Complex double z3 = 3.0 + 4.0i;
    _Complex double res3 = r3 * z3;
    printf("real*complex: %.1f %.1f\n", __real__ res3, __imag__ res3);

    /* Test 4: _Complex double * double → _Complex double (commutativity) */
    _Complex double res4 = z3 * r3;
    printf("complex*real: %.1f %.1f\n", __real__ res4, __imag__ res4);

    /* Test 5: double - _Complex double → _Complex double
     * 5.0 - (1.0+2.0i) = (4.0) + (-2.0)i */
    double r5 = 5.0;
    _Complex double z5 = 1.0 + 2.0i;
    _Complex double res5 = r5 - z5;
    printf("real-complex: %.1f %.1f\n", __real__ res5, __imag__ res5);

    /* Test 6: _Complex double - double → _Complex double
     * (1.0+2.0i) - 5.0 = (-4.0+2.0i) */
    _Complex double res6 = z5 - r5;
    printf("complex-real: %.1f %.1f\n", __real__ res6, __imag__ res6);

    /* Test 7: _Complex double / double → _Complex double
     * (6.0+8.0i) / 2.0: denom=4, real=12/4=3, imag=16/4=4 */
    _Complex double z7 = 6.0 + 8.0i;
    double r7 = 2.0;
    _Complex double res7 = z7 / r7;
    printf("complex/real: %.1f %.1f\n", __real__ res7, __imag__ res7);

    /* Test 8: int + _Complex double → _Complex double
     * Integer 3 promoted to double 3.0, then to (3.0+0.0i). */
    int i8 = 3;
    _Complex double z8 = 1.0 + 2.0i;
    _Complex double res8 = i8 + z8;
    printf("int+complex: %.1f %.1f\n", __real__ res8, __imag__ res8);

    /* Test 9: float + _Complex double → _Complex double
     * Float 1.5 promoted to double, then to complex double. */
    float f9 = 1.5f;
    _Complex double z9 = 2.5 + 3.5i;
    _Complex double res9 = f9 + z9;
    printf("float+cdouble: %.1f %.1f\n", __real__ res9, __imag__ res9);

    /* Test 10: _Complex float + double → _Complex double
     * _Complex float (1.0+1.0i) promoted to _Complex double,
     * then double 2.0 promoted to (2.0+0.0i). Result: (3.0+1.0i). */
    _Complex float zf10 = 1.0f + 1.0fi;
    double d10 = 2.0;
    _Complex double res10 = zf10 + d10;
    printf("cfloat+double: %.1f %.1f\n", __real__ res10, __imag__ res10);

    /* Test 11: Real assigned to _Complex (C11 §6.3.1.7)
     * Imaginary part must be positive zero. */
    _Complex double z11 = 42.0;
    printf("real_to_complex: %.1f %.1f\n", __real__ z11, __imag__ z11);

    /* Test 12: _Complex assigned to real (C11 §6.3.1.7)
     * Imaginary part discarded, real part converted. */
    _Complex double z12 = 7.0 + 9.0i;
    double d12 = z12;
    printf("complex_to_real: %.1f\n", d12);

    /* Test 13: Chained mixed expression: int * complex + float
     * 2*(1.0+3.0i) + 10.0 = (2.0+6.0i) + (10.0+0.0i) = (12.0+6.0i) */
    int i13 = 2;
    _Complex double z13 = 1.0 + 3.0i;
    float f13 = 10.0f;
    _Complex double res13 = i13 * z13 + f13;
    printf("chain: %.1f %.1f\n", __real__ res13, __imag__ res13);

    /* Test 14: Negation of mixed arithmetic result
     * -(z + 1.0) where z = (3.0+4.0i): (3.0+4.0i)+1.0 = (4.0+4.0i), negated = (-4.0-4.0i) */
    _Complex double z14 = 3.0 + 4.0i;
    _Complex double res14 = -(z14 + 1.0);
    printf("neg_mixed: %.1f %.1f\n", __real__ res14, __imag__ res14);

    return 0;
}
