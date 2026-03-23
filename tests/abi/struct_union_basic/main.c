/*
 * ABI integration test: basic union layout and type-punning
 *
 * Verifies that CCC correctly implements basic union layout for
 * union U { char a; int b; double c; } across all four target
 * architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * Layout analysis:
 *
 *   On LP64 (x86-64, AArch64, RISC-V 64):
 *     char a:   size 1, align 1
 *     int b:    size 4, align 4
 *     double c: size 8, align 8
 *     Union size  = max(1, 4, 8) = 8
 *     Union align = max(1, 4, 8) = 8
 *
 *   On i686 (ILP32):
 *     char a:   size 1, align 1
 *     int b:    size 4, align 4
 *     double c: size 8, align 4  (per i386 System V ABI)
 *     Union size  = max(1, 4, 8) = 8
 *     Union align = max(1, 4, 4) = 4
 *
 * sizeof(union U) is 8 on ALL architectures.
 * alignof(union U) differs: 8 on LP64, 4 on i686.
 *
 * All union members start at offset 0 (they overlap in memory).
 * Union type-punning (write through one member, read through another)
 * is valid in C11 (C11 section 6.5.2.3, footnote 95).
 * All CCC targets are little-endian.
 *
 * Return-by-value through ABI is tested via noinline helper functions.
 *   LP64: union U (8 bytes) fits in a single register.
 *   i686: union U (8 bytes) returned via eax:edx or hidden pointer.
 *
 * Tests (10 total):
 *   1. sizeof_union         - sizeof(union U) == 8
 *   2. alignof_union        - alignment 8 (LP64) or 4 (i686)
 *   3. members_overlap      - all members at offset 0
 *   4. store_char           - write/read char member
 *   5. store_int            - write/read int member
 *   6. store_double         - write/read double member
 *   7. return_union_char    - return union via ABI (char set)
 *   8. return_union_int     - return union via ABI (int set)
 *   9. return_union_double  - return union via ABI (double set)
 *  10. type_punning         - write int 0x00000041, read char 'A'
 */

#include <stdio.h>

/* The union under test — must match the specification exactly. */
union U { char a; int b; double c; };

/* -----------------------------------------------------------------------
 * Noinline helper functions that return the union through the ABI.
 * __attribute__((noinline)) prevents the optimizer from inlining the call,
 * ensuring the actual ABI return mechanism is exercised.
 * ----------------------------------------------------------------------- */

__attribute__((noinline))
static union U make_u_char(char v) {
    union U u;
    u.a = v;
    return u;
}

__attribute__((noinline))
static union U make_u_int(int v) {
    union U u;
    u.b = v;
    return u;
}

__attribute__((noinline))
static union U make_u_double(double v) {
    union U u;
    u.c = v;
    return u;
}

/* -----------------------------------------------------------------------
 * main — runs 10 test cases and reports results.
 * Returns 0 when all tests pass, or the number of failures otherwise.
 * ----------------------------------------------------------------------- */

int main(void) {
    int failures = 0;
    union U u;

    /* Test 1: sizeof_union — union U is 8 bytes on all architectures.
     * sizeof(union U) = max(sizeof(char), sizeof(int), sizeof(double))
     *                  = max(1, 4, 8) = 8. */
    if (sizeof(union U) == 8)
        printf("sizeof_union: OK\n");
    else {
        printf("sizeof_union: FAIL (got %d)\n", (int)sizeof(union U));
        failures++;
    }

    /* Test 2: alignof_union — alignment is 8 on LP64, 4 on i686.
     * Uses an AlignHelper struct to measure alignment at runtime,
     * avoiding _Alignof dependency. After char c (1 byte), the compiler
     * inserts padding to align union U. The offset of u within
     * AlignHelper equals the alignment of union U.
     * On LP64, double alignment = 8 → union alignment = 8.
     * On i686, double alignment = 4 → union alignment = 4.
     * sizeof(void*) == 4 detects i686 at runtime. */
    {
        struct AlignHelper { char c; union U u; };
        struct AlignHelper ah;
        int union_align = (int)((char *)&ah.u - (char *)&ah);
        int expected_align = (sizeof(void*) == 4) ? 4 : 8;
        if (union_align == expected_align)
            printf("alignof_union: OK\n");
        else {
            printf("alignof_union: FAIL (got %d, expected %d)\n", union_align, expected_align);
            failures++;
        }
    }

    /* Test 3: members_overlap — all 3 union members share the same address.
     * Fundamental union property: every member starts at offset 0. */
    {
        if ((char *)&u.a == (char *)&u.b && (char *)&u.b == (char *)&u.c)
            printf("members_overlap: OK\n");
        else {
            printf("members_overlap: FAIL\n");
            failures++;
        }
    }

    /* Test 4: store_char — store 'X' to char member and read back. */
    {
        u.a = 'X';
        if (u.a == 'X')
            printf("store_char: OK\n");
        else {
            printf("store_char: FAIL (got %d)\n", (int)u.a);
            failures++;
        }
    }

    /* Test 5: store_int — store 42 to int member and read back. */
    {
        u.b = 42;
        if (u.b == 42)
            printf("store_int: OK\n");
        else {
            printf("store_int: FAIL (got %d)\n", u.b);
            failures++;
        }
    }

    /* Test 6: store_double — store 3.75 to double member and read back.
     * 3.75 is exactly representable in IEEE 754, so == comparison is safe. */
    {
        u.c = 3.75;
        if (u.c == 3.75)
            printf("store_double: OK\n");
        else {
            printf("store_double: FAIL\n");
            failures++;
        }
    }

    /* Test 7: return_union_char — return union with char set through ABI.
     * make_u_char is noinline, so the union passes through the ABI return path. */
    {
        union U r = make_u_char('Z');
        if (r.a == 'Z')
            printf("return_union_char: OK\n");
        else {
            printf("return_union_char: FAIL (got %d)\n", (int)r.a);
            failures++;
        }
    }

    /* Test 8: return_union_int — return union with int set through ABI. */
    {
        union U r = make_u_int(99999);
        if (r.b == 99999)
            printf("return_union_int: OK\n");
        else {
            printf("return_union_int: FAIL (got %d)\n", r.b);
            failures++;
        }
    }

    /* Test 9: return_union_double — return union with double set through ABI.
     * 1.5 is exactly representable in IEEE 754. */
    {
        union U r = make_u_double(1.5);
        if (r.c == 1.5)
            printf("return_union_double: OK\n");
        else {
            printf("return_union_double: FAIL\n");
            failures++;
        }
    }

    /* Test 10: type_punning — write int 0x00000041, read char member.
     * On all CCC targets (little-endian), the lowest byte of int 0x00000041
     * is 0x41 = 'A' (ASCII 65). The char member a occupies that same lowest
     * byte. Union type-punning is valid in C11 (section 6.5.2.3 footnote 95). */
    {
        u.b = 0x00000041;
        if (u.a == 'A')
            printf("type_punning: OK\n");
        else {
            printf("type_punning: FAIL (got %d)\n", (int)u.a);
            failures++;
        }
    }

    /* Summary */
    if (failures == 0)
        printf("All union basic tests passed\n");
    return failures;
}
