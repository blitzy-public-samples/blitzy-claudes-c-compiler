/*
 * ABI integration test: anonymous struct member layout and direct access
 *
 * Validates C11 §6.7.2.1p13 anonymous struct semantics:
 *   - Members of an anonymous struct are directly accessible on the outer struct
 *   - Struct layout with anonymous struct follows standard padding/alignment rules
 *   - Alignment propagation from anonymous struct members is correct
 *   - double field after anonymous struct is properly aligned per architecture ABI
 *   - Returning the struct through the ABI preserves all field values
 *
 * Layout (LP64: x86-64, AArch64, RISC-V 64):
 *   offset 0:  int x       (4 bytes)
 *   offset 4:  char a      (1 byte + 3 padding)
 *   offset 8:  int b       (4 bytes)
 *   offset 12: 4 padding   (for double 8-byte alignment)
 *   offset 16: double y    (8 bytes)
 *   sizeof = 24, alignof = 8
 *
 * Layout (ILP32: i686):
 *   offset 0:  int x       (4 bytes)
 *   offset 4:  char a      (1 byte + 3 padding)
 *   offset 8:  int b       (4 bytes)
 *   offset 12: double y    (8 bytes, 4-byte aligned on i686)
 *   sizeof = 20, alignof = 4
 */

#include <stdio.h>
#include <stddef.h>

/* Struct under test: anonymous struct (no tag, no member name) */
struct S {
    int x;
    struct { char a; int b; };
    double y;
};

/* Helper struct to detect double alignment in struct context at runtime.
 * offsetof(DoubleAlign, d) equals the alignment of double in struct context:
 * 8 on LP64 targets, 4 on i686. This avoids _Alignof dependency. */
struct DoubleAlign { char c; double d; };

/* Noinline return function — forces ABI return convention (sret on all archs
 * because struct S > 16 bytes on LP64 and > 8 bytes on i686). */
__attribute__((noinline))
static struct S make_s(int xv, char av, int bv, double yv) {
    struct S s;
    s.x = xv;
    s.a = av;
    s.b = bv;
    s.y = yv;
    return s;
}

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: access_anon_a — Direct access to anonymous struct member a */
    s.a = 'Z';
    if (s.a == 'Z')
        printf("access_anon_a: OK\n");
    else {
        printf("access_anon_a: FAIL (got %d)\n", (int)s.a);
        failures++;
    }

    /* Test 2: access_anon_b — Direct access to anonymous struct member b */
    s.b = 12345;
    if (s.b == 12345)
        printf("access_anon_b: OK\n");
    else {
        printf("access_anon_b: FAIL (got %d)\n", s.b);
        failures++;
    }

    /* Test 3: offset_x — x at offset 0 */
    {
        int off = (int)((char *)&s.x - (char *)&s);
        if (off == 0)
            printf("offset_x: OK\n");
        else {
            printf("offset_x: FAIL (offset=%d)\n", off);
            failures++;
        }
    }

    /* Test 4: offset_a — a at offset sizeof(int) */
    {
        int off = (int)((char *)&s.a - (char *)&s);
        if (off == (int)sizeof(int))
            printf("offset_a: OK\n");
        else {
            printf("offset_a: FAIL (offset=%d, expected=%d)\n", off, (int)sizeof(int));
            failures++;
        }
    }

    /* Test 5: offset_b — b at offset 2*sizeof(int) */
    {
        int off = (int)((char *)&s.b - (char *)&s);
        if (off == 2 * (int)sizeof(int))
            printf("offset_b: OK\n");
        else {
            printf("offset_b: FAIL (offset=%d, expected=%d)\n", off, 2 * (int)sizeof(int));
            failures++;
        }
    }

    /* Test 6: offset_y — y properly aligned after anonymous struct */
    {
        struct DoubleAlign da;
        int dalign = (int)((char *)&da.d - (char *)&da);
        int off = (int)((char *)&s.y - (char *)&s);
        int anon_end = 3 * (int)sizeof(int);
        int expected = anon_end;
        if (expected % dalign != 0)
            expected += dalign - (expected % dalign);
        if (off == expected)
            printf("offset_y: OK\n");
        else {
            printf("offset_y: FAIL (offset=%d, expected=%d)\n", off, expected);
            failures++;
        }
    }

    /* Test 7: sizeof_struct — verify total struct size */
    {
        struct DoubleAlign da;
        int dalign = (int)((char *)&da.d - (char *)&da);
        int off_y = (int)((char *)&s.y - (char *)&s);
        int expected_size = off_y + (int)sizeof(double);
        int salign = dalign > (int)sizeof(int) ? dalign : (int)sizeof(int);
        if (expected_size % salign != 0)
            expected_size += salign - (expected_size % salign);
        if ((int)sizeof(struct S) == expected_size)
            printf("sizeof_struct: OK\n");
        else {
            printf("sizeof_struct: FAIL (actual=%d, expected=%d)\n",
                   (int)sizeof(struct S), expected_size);
            failures++;
        }
    }

    /* Test 8: store_load_all — store and verify all fields through struct */
    {
        s.x = 10;
        s.a = 'A';
        s.b = 42;
        s.y = 2.71828;
        if (s.x == 10 && s.a == 'A' && s.b == 42 && s.y == 2.71828)
            printf("store_load_all: OK\n");
        else {
            printf("store_load_all: FAIL\n");
            failures++;
        }
    }

    /* Test 9: return_struct — return struct with anonymous member through ABI */
    {
        struct S r = make_s(99, 'Q', 777, 1.5);
        if (r.x == 99 && r.a == 'Q' && r.b == 777 && r.y == 1.5)
            printf("return_struct: OK\n");
        else {
            printf("return_struct: FAIL (x=%d, a=%d, b=%d)\n", r.x, (int)r.a, r.b);
            failures++;
        }
    }

    /* Test 10: anon_no_overlap — anonymous members don't overlap with named fields */
    {
        s.x = 0;
        s.a = 0;
        s.b = 0;
        s.y = 0.0;
        s.b = 0x7F7F7F7F;
        if (s.x == 0 && s.a == 0 && s.y == 0.0)
            printf("anon_no_overlap: OK\n");
        else {
            printf("anon_no_overlap: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All anonymous struct tests passed\n");
    return failures;
}
