/*
 * ABI integration test: anonymous union inside struct layout and direct access
 *
 * Validates C11 §6.7.2.1p13 anonymous union semantics:
 *   - Members of an anonymous union within an outer struct are directly
 *     accessible (e.g., s.i instead of s.anon.i)
 *   - Anonymous union members share the same memory location (union semantics)
 *   - Struct layout with an anonymous union follows correct padding/alignment
 *   - The char c field after the anonymous union is properly placed per ABI
 *   - Returning the struct through the ABI preserves all field values
 *
 * Layout (LP64: x86-64, AArch64, RISC-V 64):
 *   offset 0:  int tag     (4 bytes)
 *   offset 4:  4 padding   (for double 8-byte alignment)
 *   offset 8:  union { int i; double d; }  (8 bytes, align 8)
 *   offset 16: char c      (1 byte)
 *   offset 17: 7 padding   (for 8-byte struct alignment)
 *   sizeof = 24, alignof = 8
 *
 * Layout (ILP32: i686):
 *   offset 0:  int tag     (4 bytes)
 *   offset 4:  union { int i; double d; }  (8 bytes, align 4)
 *   offset 12: char c      (1 byte)
 *   offset 13: 3 padding   (for 4-byte struct alignment)
 *   sizeof = 16, alignof = 4
 */

#include <stdio.h>

/* Struct under test: anonymous union (no tag name, no member name) — C11 feature */
struct S {
    int tag;
    union { int i; double d; };
    char c;
};

/* Helper struct to detect double alignment in struct context at runtime.
 * (char *)&da.d - (char *)&da equals the alignment of double in struct context:
 * 8 on LP64 targets, 4 on i686. This avoids _Alignof dependency. */
struct DoubleAlign { char c; double d; };

/* Noinline return function — forces ABI return convention.
 * struct S is 24 bytes on LP64 (> 16) and 16 bytes on i686 (> 8),
 * so it is always returned via hidden pointer (sret/memory). */
__attribute__((noinline))
static struct S make_s(int tv, double dv, char cv) {
    struct S s;
    s.tag = tv;
    s.d = dv;
    s.c = cv;
    return s;
}

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: access_union_i — Direct access to anonymous union member i */
    s.i = 42;
    if (s.i == 42)
        printf("access_union_i: OK\n");
    else {
        printf("access_union_i: FAIL (got %d)\n", s.i);
        failures++;
    }

    /* Test 2: access_union_d — Direct access to anonymous union member d */
    s.d = 1.5;
    if (s.d == 1.5)
        printf("access_union_d: OK\n");
    else {
        printf("access_union_d: FAIL\n");
        failures++;
    }

    /* Test 3: offset_tag — tag at offset 0 */
    {
        int off = (int)((char *)&s.tag - (char *)&s);
        if (off == 0)
            printf("offset_tag: OK\n");
        else {
            printf("offset_tag: FAIL (offset=%d)\n", off);
            failures++;
        }
    }

    /* Test 4: offset_union — anonymous union at correct alignment-dependent offset */
    {
        struct DoubleAlign da;
        int dalign = (int)((char *)&da.d - (char *)&da);
        int union_align = dalign > (int)sizeof(int) ? dalign : (int)sizeof(int);
        int expected = (int)sizeof(int);
        if (expected % union_align != 0)
            expected += union_align - (expected % union_align);
        int off = (int)((char *)&s.i - (char *)&s);
        if (off == expected)
            printf("offset_union: OK\n");
        else {
            printf("offset_union: FAIL (offset=%d, expected=%d)\n", off, expected);
            failures++;
        }
    }

    /* Test 5: offset_c — c at correct offset after anonymous union */
    {
        struct DoubleAlign da;
        int dalign = (int)((char *)&da.d - (char *)&da);
        int union_align = dalign > (int)sizeof(int) ? dalign : (int)sizeof(int);
        int union_offset = (int)sizeof(int);
        if (union_offset % union_align != 0)
            union_offset += union_align - (union_offset % union_align);
        int union_size = (int)sizeof(double);
        int expected = union_offset + union_size;
        int off = (int)((char *)&s.c - (char *)&s);
        if (off == expected)
            printf("offset_c: OK\n");
        else {
            printf("offset_c: FAIL (offset=%d, expected=%d)\n", off, expected);
            failures++;
        }
    }

    /* Test 6: sizeof_struct — verify total struct size */
    {
        struct DoubleAlign da;
        int dalign = (int)((char *)&da.d - (char *)&da);
        int union_align = dalign > (int)sizeof(int) ? dalign : (int)sizeof(int);
        int union_offset = (int)sizeof(int);
        if (union_offset % union_align != 0)
            union_offset += union_align - (union_offset % union_align);
        int offset_c_val = union_offset + (int)sizeof(double);
        int raw_size = offset_c_val + 1;
        int struct_align = union_align;
        if (raw_size % struct_align != 0)
            raw_size += struct_align - (raw_size % struct_align);
        if ((int)sizeof(struct S) == raw_size)
            printf("sizeof_struct: OK\n");
        else {
            printf("sizeof_struct: FAIL (actual=%d, expected=%d)\n",
                   (int)sizeof(struct S), raw_size);
            failures++;
        }
    }

    /* Test 7: union_overlap — anonymous union members share the same address */
    {
        if ((char *)&s.i == (char *)&s.d)
            printf("union_overlap: OK\n");
        else {
            printf("union_overlap: FAIL\n");
            failures++;
        }
    }

    /* Test 8: store_load_all — store and verify all fields through struct */
    {
        s.tag = 10;
        s.d = 1.5;
        s.c = 'A';
        if (s.tag == 10 && s.d == 1.5 && s.c == 'A')
            printf("store_load_all: OK\n");
        else {
            printf("store_load_all: FAIL\n");
            failures++;
        }
    }

    /* Test 9: return_struct — return struct with anonymous union through ABI */
    {
        struct S r = make_s(99, 2.5, 'Q');
        if (r.tag == 99 && r.d == 2.5 && r.c == 'Q')
            printf("return_struct: OK\n");
        else {
            printf("return_struct: FAIL (tag=%d, c=%d)\n", r.tag, (int)r.c);
            failures++;
        }
    }

    /* Test 10: union_overwrite — writing to one union member overwrites the other.
     * Sets s.i to 0x12345678, then writes s.d = 0.0. IEEE 754 double 0.0 is
     * all-zero-bits (0x0000000000000000), so the first 4 bytes (where int i
     * lives on all little-endian CCC targets) become 0. */
    {
        s.i = 0x12345678;
        s.d = 0.0;
        if (s.i == 0)
            printf("union_overwrite: OK\n");
        else {
            printf("union_overwrite: FAIL (i=%d after d=0.0)\n", s.i);
            failures++;
        }
    }

    if (failures == 0)
        printf("All anonymous union tests passed\n");
    return failures;
}
