/*
 * ABI integration test: union member inside struct layout
 *
 * Verifies that CCC correctly implements the layout of a struct containing
 * a union member across all four target architectures (x86-64, AArch64,
 * RISC-V 64, i686).
 *
 * Types under test:
 *   union U  { int x; short y; };
 *   struct S { char a; union U u; double b; };
 *
 * Layout analysis (all architectures):
 *
 *   union U:
 *     sizeof  = max(sizeof(int), sizeof(short)) = max(4, 2) = 4
 *     alignof = max(alignof(int), alignof(short)) = max(4, 2) = 4
 *
 *   struct S on LP64 (x86-64, AArch64, RISC-V 64):
 *     char a   : offset 0, size 1, align 1
 *     [pad 3]  : offsets 1-3 (align union U to 4)
 *     union U u: offset 4, size 4, align 4
 *     double b : offset 8, size 8, align 8 (8 already aligned)
 *     sizeof(struct S) = align_up(16, max(1,4,8)=8) = 16
 *
 *   struct S on i686 (ILP32):
 *     char a   : offset 0, size 1, align 1
 *     [pad 3]  : offsets 1-3 (align union U to 4)
 *     union U u: offset 4, size 4, align 4
 *     double b : offset 8, size 8, align 4 (double align=4 on i686)
 *     sizeof(struct S) = align_up(16, max(1,4,4)=4) = 16
 *
 *   Key insight: offsets a=0, u=4, b=8, sizeof(S)=16 are IDENTICAL on
 *   all four architectures. Only struct alignment differs (8 vs 4) but
 *   both yield the same total size.
 *
 * All floating-point literal values (3.75, 1.5) are exactly representable
 * in IEEE 754 binary64, so exact equality comparison is safe.
 *
 * All CCC targets are little-endian, so union type-punning with specific
 * byte patterns (0x00000042) yields deterministic results.
 */

#include <stdio.h>

union U { int x; short y; };
struct S { char a; union U u; double b; };

__attribute__((noinline))
static struct S make_s(char av, int xv, double bv) {
    struct S s;
    s.a = av;
    s.u.x = xv;
    s.b = bv;
    return s;
}

int main(void) {
    int failures = 0;
    struct S s;
    union U u;

    /* Test 1: sizeof_union — union U is 4 bytes */
    if (sizeof(union U) == 4)
        printf("sizeof_union: OK\n");
    else {
        printf("sizeof_union: FAIL (got %d)\n", (int)sizeof(union U));
        failures++;
    }

    /* Test 2: alignof_union — union U alignment is 4 (via AlignHelper) */
    {
        struct AlignHelper { char c; union U u; };
        struct AlignHelper ah;
        int union_align = (int)((char *)&ah.u - (char *)&ah);
        if (union_align == 4)
            printf("alignof_union: OK\n");
        else {
            printf("alignof_union: FAIL (got %d)\n", union_align);
            failures++;
        }
    }

    /* Test 3: offset_a — char a at offset 0 */
    {
        int off = (int)((char *)&s.a - (char *)&s);
        if (off == 0)
            printf("offset_a: OK\n");
        else {
            printf("offset_a: FAIL (offset=%d)\n", off);
            failures++;
        }
    }

    /* Test 4: offset_u — union U u at offset 4 */
    {
        int off = (int)((char *)&s.u - (char *)&s);
        if (off == 4)
            printf("offset_u: OK\n");
        else {
            printf("offset_u: FAIL (offset=%d, expected=4)\n", off);
            failures++;
        }
    }

    /* Test 5: offset_b — double b at offset 8 */
    {
        int off = (int)((char *)&s.b - (char *)&s);
        if (off == 8)
            printf("offset_b: OK\n");
        else {
            printf("offset_b: FAIL (offset=%d, expected=8)\n", off);
            failures++;
        }
    }

    /* Test 6: sizeof_struct — struct S is 16 bytes */
    if ((int)sizeof(struct S) == 16)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %d, expected=16)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 7: union_overlap — union members within struct share the same address */
    {
        if ((char *)&s.u.x == (char *)&s.u.y)
            printf("union_overlap: OK\n");
        else {
            printf("union_overlap: FAIL\n");
            failures++;
        }
    }

    /* Test 8: store_load_all — store to all fields and verify */
    {
        s.a = 'M';
        s.u.x = 12345;
        s.b = 3.75;
        if (s.a == 'M' && s.u.x == 12345 && s.b == 3.75)
            printf("store_load_all: OK\n");
        else {
            printf("store_load_all: FAIL\n");
            failures++;
        }
    }

    /* Test 9: return_struct — return struct with union member through ABI */
    {
        struct S r = make_s('Z', 99999, 1.5);
        if (r.a == 'Z' && r.u.x == 99999 && r.b == 1.5)
            printf("return_struct: OK\n");
        else {
            printf("return_struct: FAIL (a=%d, u.x=%d)\n", (int)r.a, r.u.x);
            failures++;
        }
    }

    /* Test 10: union_write_int_read_short — write int, read short (union type-punning) */
    {
        s.u.x = 0x00000042;
        if (s.u.y == 0x0042)
            printf("union_write_int_read_short: OK\n");
        else {
            printf("union_write_int_read_short: FAIL (y=%d)\n", (int)s.u.y);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct union in struct tests passed\n");
    return failures;
}
