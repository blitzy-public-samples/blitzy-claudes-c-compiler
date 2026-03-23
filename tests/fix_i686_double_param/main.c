/*
 * Regression test: fix_i686_double_param_high_word_store
 *
 * Validates that on i686, both the low and high 32-bit words of a double
 * parameter are correctly stored when copying from the stack to a local alloca.
 *
 * Bug description:
 *   On i686, double parameters are passed on the stack as two 32-bit words.
 *   The codegen copies these to a local alloca using two movl instructions
 *   (one for the low word, one for the high word). When <stdlib.h> and
 *   <math.h> are included (bringing in long double function declarations
 *   such as sqrtl, sinl, cosl, etc.), the high-word movl store
 *   (movl %eax, -4(%ebp)) is dropped, leaving the upper 32 bits of the
 *   double uninitialized (effectively zero).
 *
 * Original reproducer: compiler_suite_0149_0011
 *   f01(double) -> unsigned int returned 0 instead of 10.
 *
 * IEEE 754 analysis for test value 10.0:
 *   Double 10.0 = 0x4024000000000000
 *   Low  32 bits (lower address, little-endian): 0x00000000
 *   High 32 bits (higher address, little-endian): 0x40240000
 *
 *   With the bug (high word dropped):
 *     Bit pattern becomes 0x0000000000000000 = 0.0
 *     (unsigned int)0.0 = 0   <-- BUG RESULT
 *
 *   With the fix (both words stored):
 *     Bit pattern is correct 0x4024000000000000 = 10.0
 *     (unsigned int)10.0 = 10  <-- CORRECT RESULT
 *
 * CRITICAL: Both <stdlib.h> and <math.h> MUST be included to trigger the
 * original bug. These headers declare long double math function variants
 * that cause the i686 codegen to incorrectly drop the high-word store for
 * subsequent double parameter copies.
 */

#include <stdlib.h>
#include <math.h>
#include <stdio.h>

/* Exact function signature from the original compiler_suite_0149_0011 reproducer.
 * On i686, the double parameter 'd' is passed via two 32-bit stack words.
 * The codegen must emit two movl instructions to copy both the low word
 * and the high word into the local alloca. */
static unsigned int f01(double d) {
    return (unsigned int)d;
}

int main(void) {
    unsigned int result = f01(10.0);
    printf("%u\n", result);
    return 0;
}
