/*
 * ABI integration test: union alignment propagation from nested struct members
 *
 * Verifies that CCC correctly implements the C standard's union alignment rule:
 * a union's alignment is the maximum alignment of ALL its members.  When a member
 * is a struct, the struct's alignment (which itself is the max alignment of its
 * fields) must propagate to the union.
 *
 * Types under test:
 *   struct S { char a; double b; };
 *   union  U { int x; struct S y; };
 *
 * On LP64 (x86-64, AArch64, RISC-V 64):
 *   double alignment = 8
 *   struct S: size=16, align=8  (char@0, 7 pad, double@8)
 *   union  U: size=16, align=8  (max(4, 8)=8)
 *
 * On ILP32 (i686):
 *   double alignment = 4  (per i386 System V ABI)
 *   struct S: size=12, align=4  (char@0, 3 pad, double@4)
 *   union  U: size=12, align=4  (max(4, 4)=4)
 *
 * All alignment detection is done at runtime via helper structs, avoiding
 * dependency on _Alignof (a C11 feature under development).
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual ABI return convention.
 *
 * No architecture-specific preprocessor guards are used — the test is
 * portable across all 4 CCC targets.  Float comparisons use exactly
 * representable IEEE 754 values (2.5 and 3.75).
 */

#include <stdio.h>

/* ── Type definitions ─────────────────────────────────────────────────────── */

struct S { char a; double b; };
union U { int x; struct S y; };

/* ── Helper structs for runtime alignment detection ───────────────────────── */

/* Determines the alignment of double in struct context at runtime.
 * (char *)&da.d - (char *)&da equals the alignment of double (8 on LP64, 4 on i686). */
struct DoubleAlign { char c; double d; };

/* Determines the alignment of union U at runtime.
 * (char *)&uah.u - (char *)&uah equals the alignment of union U. */
struct UnionAlignHelper { char c; union U u; };

/* Determines the alignment of struct S at runtime.
 * (char *)&sah.s - (char *)&sah equals the alignment of struct S. */
struct StructAlignHelper { char c; struct S s; };

/* ── Noinline return functions for ABI return path testing ────────────────── */

__attribute__((noinline))
static union U make_u_int(int v) {
    union U u;
    u.x = v;
    return u;
}

__attribute__((noinline))
static union U make_u_struct(char a, double b) {
    union U u;
    u.y.a = a;
    u.y.b = b;
    return u;
}

/* ── Main: 10 test cases ──────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    union U u;
    struct DoubleAlign da;
    struct UnionAlignHelper uah;
    struct StructAlignHelper sah;

    /* Compute expected alignment of double in struct context */
    int dalign = (int)((char *)&da.d - (char *)&da);

    /* Expected struct S layout */
    int s_align = dalign;  /* struct align = max(1, dalign) = dalign since dalign >= 1 */
    int s_b_offset = dalign;  /* char(1) + padding to dalign boundary = dalign */
    int s_size = s_b_offset + 8;  /* offset_of(b) + sizeof(double) */
    if (s_size % s_align != 0)
        s_size += s_align - (s_size % s_align);  /* round up to struct alignment */

    /* Expected union U layout */
    int u_align = s_align > 4 ? s_align : 4;  /* max(alignof(int), alignof(struct S)) */
    int u_size = s_size > 4 ? s_size : 4;  /* max(sizeof(int), sizeof(struct S)) */
    if (u_size % u_align != 0)
        u_size += u_align - (u_size % u_align);  /* round up to union alignment */

    /* Test 1: sizeof_struct_s — verify struct S size matches expected */
    if ((int)sizeof(struct S) == s_size)
        printf("sizeof_struct_s: OK\n");
    else {
        printf("sizeof_struct_s: FAIL (actual=%d, expected=%d)\n",
               (int)sizeof(struct S), s_size);
        failures++;
    }

    /* Test 2: alignof_struct_s — verify struct S alignment propagates from double */
    {
        int actual_align = (int)((char *)&sah.s - (char *)&sah);
        if (actual_align == s_align)
            printf("alignof_struct_s: OK\n");
        else {
            printf("alignof_struct_s: FAIL (actual=%d, expected=%d)\n",
                   actual_align, s_align);
            failures++;
        }
    }

    /* Test 3: sizeof_union_u — verify union size equals sizeof(struct S) since struct S > int */
    if ((int)sizeof(union U) == u_size)
        printf("sizeof_union_u: OK\n");
    else {
        printf("sizeof_union_u: FAIL (actual=%d, expected=%d)\n",
               (int)sizeof(union U), u_size);
        failures++;
    }

    /* Test 4: alignof_union_u — verify union alignment equals max of member alignments */
    {
        int actual_align = (int)((char *)&uah.u - (char *)&uah);
        if (actual_align == u_align)
            printf("alignof_union_u: OK\n");
        else {
            printf("alignof_union_u: FAIL (actual=%d, expected=%d)\n",
                   actual_align, u_align);
            failures++;
        }
    }

    /* Test 5: union_size_ge_all — verify sizeof(union U) >= sizeof of each member */
    if ((int)sizeof(union U) >= (int)sizeof(int) &&
        (int)sizeof(union U) >= (int)sizeof(struct S))
        printf("union_size_ge_all: OK\n");
    else {
        printf("union_size_ge_all: FAIL (union=%d, int=%d, struct=%d)\n",
               (int)sizeof(union U), (int)sizeof(int), (int)sizeof(struct S));
        failures++;
    }

    /* Test 6: union_align_propagation — verify union alignment matches struct S alignment */
    {
        int struct_align = (int)((char *)&sah.s - (char *)&sah);
        int union_align = (int)((char *)&uah.u - (char *)&uah);
        if (union_align >= struct_align)
            printf("union_align_propagation: OK\n");
        else {
            printf("union_align_propagation: FAIL (union_align=%d, struct_align=%d)\n",
                   union_align, struct_align);
            failures++;
        }
    }

    /* Test 7: offset_y_a — struct S member y.a at offset 0 within union */
    {
        int off = (int)((char *)&u.y.a - (char *)&u);
        if (off == 0)
            printf("offset_y_a: OK\n");
        else {
            printf("offset_y_a: FAIL (offset=%d)\n", off);
            failures++;
        }
    }

    /* Test 8: offset_y_b — struct S member y.b at correct aligned offset within union */
    {
        int off = (int)((char *)&u.y.b - (char *)&u);
        if (off == s_b_offset)
            printf("offset_y_b: OK\n");
        else {
            printf("offset_y_b: FAIL (offset=%d, expected=%d)\n", off, s_b_offset);
            failures++;
        }
    }

    /* Test 9: return_union_struct — return union with struct member through ABI */
    {
        union U r = make_u_struct('K', 2.5);
        if (r.y.a == 'K' && r.y.b == 2.5)
            printf("return_union_struct: OK\n");
        else {
            printf("return_union_struct: FAIL (a=%d, b=%f)\n",
                   (int)r.y.a, r.y.b);
            failures++;
        }
    }

    /* Test 10: store_load_struct_member — store values via struct member, verify all fields */
    {
        u.y.a = 'Z';
        u.y.b = 3.75;
        if (u.y.a == 'Z' && u.y.b == 3.75)
            printf("store_load_struct_member: OK\n");
        else {
            printf("store_load_struct_member: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All union alignment tests passed\n");
    return failures;
}
