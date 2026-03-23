/*
 * ABI integration test: struct layout with long double member
 *
 * Verifies that CCC correctly implements struct layout when a long double
 * member is present, including padding, alignment, and value roundtrip
 * through function call/return ABI.
 *
 * Struct under test:
 *   struct S { char a; long double b; int c; };
 *
 * On LP64 targets (long double has size 16, alignment 16):
 *   char a        at offset  0 (size 1, align 1)
 *   [padding]     15 bytes to align long double to 16
 *   long double b at offset 16 (size 16, align 16)
 *   int c         at offset 32 (size 4, align 4)
 *   [padding]     12 bytes trailing to make sizeof a multiple of 16
 *   Total sizeof(struct S) = 48
 *
 * Architecture notes:
 *   x86-64:  long double is x87 80-bit, stored in 16 bytes, aligned to 16
 *   AArch64: long double is IEEE binary128, 16 bytes, aligned to 16
 *   RISC-V:  long double is IEEE binary128, 16 bytes, aligned to 16
 *
 * SKIPPED on i686 via expected.skip.i686 because:
 *   i686 long double is x87 80-bit stored in 12 bytes with 4-byte alignment.
 *   Layout differs: sizeof=20, offsetof(b)=4, offsetof(c)=16, alignof=4.
 */

#include <stdio.h>
#include <stddef.h>

struct S {
    char a;
    long double b;
    int c;
};

/*
 * Construct a struct S from individual field values.
 * Returns the struct by value. Since sizeof(struct S) = 48 (>16 bytes),
 * all LP64 architectures use hidden pointer return (x86-64 SysV,
 * AArch64 AAPCS, RISC-V LP64D).
 */
__attribute__((noinline))
struct S make_s(char a_val, long double b_val, int c_val)
{
    struct S s;
    s.a = a_val;
    s.b = b_val;
    s.c = c_val;
    return s;
}

/*
 * Receive a struct S by value and verify each field matches expected values.
 * Returns 0 on success, nonzero error code on failure.
 * Tests the parameter passing ABI for the 48-byte struct.
 * Uses 3.0L for long double comparison which is exactly representable
 * in both x87 80-bit and IEEE binary128 formats.
 */
__attribute__((noinline))
int check_s(struct S s, char exp_a, long double exp_b, int exp_c)
{
    if (s.a != exp_a) return 1;
    if (s.b != exp_b) return 2;
    if (s.c != exp_c) return 3;
    return 0;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct S) == 48 */
    if (sizeof(struct S) == 48) {
        printf("sizeof: OK\n");
    } else {
        printf("sizeof: FAIL (got %lu, expected 48)\n",
               (unsigned long)sizeof(struct S));
        failures++;
    }

    /* Test 2: sizeof(long double) == 16 */
    if (sizeof(long double) == 16) {
        printf("sizeof_ld: OK\n");
    } else {
        printf("sizeof_ld: FAIL (got %lu, expected 16)\n",
               (unsigned long)sizeof(long double));
        failures++;
    }

    /* Test 3: offsetof(struct S, a) == 0 */
    if (offsetof(struct S, a) == 0) {
        printf("offsetof_a: OK\n");
    } else {
        printf("offsetof_a: FAIL (got %lu, expected 0)\n",
               (unsigned long)offsetof(struct S, a));
        failures++;
    }

    /* Test 4: offsetof(struct S, b) == 16 (key alignment test) */
    if (offsetof(struct S, b) == 16) {
        printf("offsetof_b: OK\n");
    } else {
        printf("offsetof_b: FAIL (got %lu, expected 16)\n",
               (unsigned long)offsetof(struct S, b));
        failures++;
    }

    /* Test 5: offsetof(struct S, c) == 32 */
    if (offsetof(struct S, c) == 32) {
        printf("offsetof_c: OK\n");
    } else {
        printf("offsetof_c: FAIL (got %lu, expected 32)\n",
               (unsigned long)offsetof(struct S, c));
        failures++;
    }

    /* Test 6: __alignof__(struct S) == 16 */
    if (__alignof__(struct S) == 16) {
        printf("alignof: OK\n");
    } else {
        printf("alignof: FAIL (got %lu, expected 16)\n",
               (unsigned long)__alignof__(struct S));
        failures++;
    }

    /* Test 7: Value roundtrip through function return */
    {
        struct S s = make_s('X', 3.0L, 77);
        if (s.a == 'X' && s.b == 3.0L && s.c == 77) {
            printf("return_value: OK\n");
        } else {
            printf("return_value: FAIL\n");
            failures++;
        }
    }

    /* Test 8: Value roundtrip through function parameter */
    {
        struct S s;
        s.a = 'X';
        s.b = 3.0L;
        s.c = 77;
        int result = check_s(s, 'X', 3.0L, 77);
        if (result == 0) {
            printf("pass_value: OK\n");
        } else {
            printf("pass_value: FAIL (code %d)\n", result);
            failures++;
        }
    }

    /* Summary */
    if (failures == 0) printf("All long double member tests passed\n");

    return failures;
}
