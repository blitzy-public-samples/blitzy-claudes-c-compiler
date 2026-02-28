/*
 * ABI integration test: __alignof__ for all basic C types
 *
 * Verifies that CCC's __alignof__ (GCC extension) returns the correct
 * preferred alignment for every basic C type across all four target
 * architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * __alignof__ returns PREFERRED alignment (also called "natural alignment"),
 * which can differ from _Alignof (C11 ABI minimum alignment) on i686.
 * Specifically, on i686:
 *   __alignof__(long long) == 8 (preferred), while _Alignof(long long) == 4
 *   __alignof__(double)    == 8 (preferred), while _Alignof(double)    == 4
 *
 * Expected __alignof__ values:
 *
 *   LP64 (x86-64, AArch64, RISC-V 64):
 *     char=1, short=2, int=4, long=8, long long=8,
 *     float=4, double=8, void*=8, long double=16
 *
 *   ILP32 (i686):
 *     char=1, short=2, int=4, long=4, long long=8 (preferred override),
 *     float=4, double=8 (preferred override), void*=4, long double=4
 *
 * Architecture detection uses runtime sizeof checks so the test produces
 * IDENTICAL output on all four architectures without preprocessor guards.
 *
 * References:
 *   src/frontend/parser/parse.rs lines 1228-1307 — alignof_type_spec and
 *       preferred_alignof_type_spec implementations
 *   src/common/types.rs lines 1367-1383 — preferred_align_ctx implementation
 */

#include <stdio.h>

int main(void) {
    int failures = 0;

    /* Architecture-independent types: hardcoded expected values */

    if (__alignof__(char) == 1)
        printf("alignof_char: OK\n");
    else {
        printf("alignof_char: FAIL (got %d, expected 1)\n", (int)__alignof__(char));
        failures++;
    }

    if (__alignof__(short) == 2)
        printf("alignof_short: OK\n");
    else {
        printf("alignof_short: FAIL (got %d, expected 2)\n", (int)__alignof__(short));
        failures++;
    }

    if (__alignof__(int) == 4)
        printf("alignof_int: OK\n");
    else {
        printf("alignof_int: FAIL (got %d, expected 4)\n", (int)__alignof__(int));
        failures++;
    }

    /* long: alignment == sizeof(long), which is 8 on LP64, 4 on ILP32 */
    {
        int expected = (int)sizeof(long);
        if ((int)__alignof__(long) == expected)
            printf("alignof_long: OK\n");
        else {
            printf("alignof_long: FAIL (got %d, expected %d)\n", (int)__alignof__(long), expected);
            failures++;
        }
    }

    /* long long: preferred alignment is 8 on ALL architectures.
     * On i686, __alignof__ returns 8 (preferred) even though _Alignof returns 4 (ABI).
     * See preferred_alignof_type_spec in parse.rs lines 1299-1302. */
    if (__alignof__(long long) == 8)
        printf("alignof_long_long: OK\n");
    else {
        printf("alignof_long_long: FAIL (got %d, expected 8)\n", (int)__alignof__(long long));
        failures++;
    }

    if (__alignof__(float) == 4)
        printf("alignof_float: OK\n");
    else {
        printf("alignof_float: FAIL (got %d, expected 4)\n", (int)__alignof__(float));
        failures++;
    }

    /* double: preferred alignment is 8 on ALL architectures.
     * On i686, __alignof__ returns 8 (preferred) even though _Alignof returns 4 (ABI).
     * See preferred_alignof_type_spec in parse.rs lines 1299-1302. */
    if (__alignof__(double) == 8)
        printf("alignof_double: OK\n");
    else {
        printf("alignof_double: FAIL (got %d, expected 8)\n", (int)__alignof__(double));
        failures++;
    }

    /* void*: alignment == sizeof(void*), which is 8 on LP64, 4 on ILP32 */
    {
        int expected = (int)sizeof(void *);
        if ((int)__alignof__(void *) == expected)
            printf("alignof_pointer: OK\n");
        else {
            printf("alignof_pointer: FAIL (got %d, expected %d)\n", (int)__alignof__(void *), expected);
            failures++;
        }
    }

    /* long double: 16 on LP64, 4 on i686 (no preferred override for long double).
     * Use sizeof(void*) to detect architecture at runtime. */
    {
        int expected;
        if (sizeof(void *) == 8)
            expected = 16;
        else
            expected = 4;
        if ((int)__alignof__(long double) == expected)
            printf("alignof_long_double: OK\n");
        else {
            printf("alignof_long_double: FAIL (got %d, expected %d)\n", (int)__alignof__(long double), expected);
            failures++;
        }
    }

    if (failures == 0)
        printf("All alignof basic type tests passed\n");

    return failures;
}
