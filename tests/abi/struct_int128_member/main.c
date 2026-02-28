/*
 * ABI integration test: struct layout with __int128 member
 *
 * Verifies that CCC correctly implements struct layout when a __int128
 * member is present, including padding, alignment, and value roundtrip
 * through function call/return ABI.
 *
 * Struct under test:
 *   struct S { char a; __int128 b; int c; };
 *
 * On LP64 targets (__int128 has size 16, alignment 16):
 *   char a      at offset  0 (size 1, align 1)
 *   [padding]   15 bytes to align __int128 to 16
 *   __int128 b  at offset 16 (size 16, align 16)
 *   int c       at offset 32 (size 4, align 4)
 *   [padding]   12 bytes trailing to make sizeof a multiple of 16
 *   Total sizeof(struct S) = 48
 *
 * SKIPPED on i686 via expected.skip.i686 because __int128 alignment
 * is only 4 on 32-bit targets.
 */

#include <stdio.h>
#include <stddef.h>

struct S {
    char a;
    __int128 b;
    int c;
};

/*
 * Construct a struct S from individual field values.
 * The __int128 member is assembled from two unsigned long long halves.
 * Tests the return value ABI for a 48-byte struct (returned via hidden
 * pointer / memory on all LP64 architectures).
 */
__attribute__((noinline))
struct S make_s(char a_val, unsigned long long b_low,
                unsigned long long b_high, int c_val)
{
    struct S s;
    s.a = a_val;
    s.b = ((__int128)b_high << 64) | (__int128)b_low;
    s.c = c_val;
    return s;
}

/*
 * Receive a struct S by value and verify each field matches expected values.
 * Extracts the __int128 member's high and low 64-bit halves via casts.
 * Returns 0 on success, nonzero error code on failure.
 * Tests the parameter passing ABI for the 48-byte struct.
 */
__attribute__((noinline))
int check_s(struct S s, char exp_a, unsigned long long exp_b_low,
            unsigned long long exp_b_high, int exp_c)
{
    unsigned long long low = (unsigned long long)s.b;
    unsigned long long high = (unsigned long long)(s.b >> 64);

    if (s.a != exp_a) return 1;
    if (low != exp_b_low) return 2;
    if (high != exp_b_high) return 3;
    if (s.c != exp_c) return 4;
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

    /* Test 2: offsetof(struct S, a) == 0 */
    if (offsetof(struct S, a) == 0) {
        printf("offsetof_a: OK\n");
    } else {
        printf("offsetof_a: FAIL (got %lu, expected 0)\n",
               (unsigned long)offsetof(struct S, a));
        failures++;
    }

    /* Test 3: offsetof(struct S, b) == 16 (key alignment test) */
    if (offsetof(struct S, b) == 16) {
        printf("offsetof_b: OK\n");
    } else {
        printf("offsetof_b: FAIL (got %lu, expected 16)\n",
               (unsigned long)offsetof(struct S, b));
        failures++;
    }

    /* Test 4: offsetof(struct S, c) == 32 */
    if (offsetof(struct S, c) == 32) {
        printf("offsetof_c: OK\n");
    } else {
        printf("offsetof_c: FAIL (got %lu, expected 32)\n",
               (unsigned long)offsetof(struct S, c));
        failures++;
    }

    /* Test 5: __alignof__(struct S) == 16 */
    if (__alignof__(struct S) == 16) {
        printf("alignof: OK\n");
    } else {
        printf("alignof: FAIL (got %lu, expected 16)\n",
               (unsigned long)__alignof__(struct S));
        failures++;
    }

    /* Test 6: Value roundtrip through function return */
    {
        struct S s = make_s('Z', 0xDEADBEEFCAFEBABEULL,
                            0x0123456789ABCDEFULL, 99);
        unsigned long long low = (unsigned long long)s.b;
        unsigned long long high = (unsigned long long)(s.b >> 64);
        if (s.a == 'Z' &&
            low == 0xDEADBEEFCAFEBABEULL &&
            high == 0x0123456789ABCDEFULL &&
            s.c == 99) {
            printf("return_value: OK\n");
        } else {
            printf("return_value: FAIL\n");
            failures++;
        }
    }

    /* Test 7: Value roundtrip through function parameter */
    {
        struct S s;
        s.a = 'Z';
        s.b = ((__int128)0x0123456789ABCDEFULL << 64) |
              (__int128)0xDEADBEEFCAFEBABEULL;
        s.c = 99;
        int result = check_s(s, 'Z', 0xDEADBEEFCAFEBABEULL,
                             0x0123456789ABCDEFULL, 99);
        if (result == 0) {
            printf("pass_value: OK\n");
        } else {
            printf("pass_value: FAIL (code %d)\n", result);
            failures++;
        }
    }

    /* Summary */
    if (failures == 0) printf("All int128 member tests passed\n");

    return failures;
}
