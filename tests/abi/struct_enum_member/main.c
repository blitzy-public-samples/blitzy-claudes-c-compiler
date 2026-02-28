/*
 * ABI integration test: struct layout with enum member
 *
 * Verifies that CCC correctly lays out struct S { char a; enum Color c; int b; }
 * with proper padding between char and enum, treating non-packed enum as int-sized
 * (4 bytes) with int alignment (4 bytes) on all four target architectures.
 *
 * Expected layout (identical on x86-64, AArch64, RISC-V 64, i686):
 *   offset 0: char a      (size 1, align 1)
 *   offset 1: 3 bytes padding (to align enum Color c to 4-byte boundary)
 *   offset 4: enum Color c (size 4, align 4)
 *   offset 8: int b       (size 4, align 4)
 *   Total: 12 bytes, struct alignment: 4
 */
#include <stdio.h>

enum Color { RED, GREEN, BLUE };

struct S {
    char a;
    enum Color c;
    int b;
};

__attribute__((noinline))
struct S make_s(char a, enum Color c, int b) {
    struct S s;
    s.a = a;
    s.c = c;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(enum Color) == sizeof(int) == 4
     * Non-packed enums with values fitting in int range are always 4 bytes.
     * RED=0, GREEN=1, BLUE=2 all fit in int.
     */
    if (sizeof(enum Color) == sizeof(int))
        printf("sizeof_enum: OK\n");
    else {
        printf("sizeof_enum: FAIL (enum=%zu, int=%zu)\n",
               sizeof(enum Color), sizeof(int));
        failures++;
    }

    /* Test 2: sizeof(struct S) == 12
     * char(1) + padding(3) + enum(4) + int(4) = 12.
     * Struct alignment is 4 (max of member alignments).
     * 12 is already a multiple of 4, so no trailing padding.
     */
    if (sizeof(struct S) == 12)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (struct=%zu, expected=12)\n",
               sizeof(struct S));
        failures++;
    }

    /* Test 3: field a at offset 0
     * First field is always at offset 0.
     */
    {
        struct S s;
        int offset = (int)((char *)&s.a - (char *)&s);
        if (offset == 0)
            printf("offset_a: OK\n");
        else {
            printf("offset_a: FAIL (offset=%d)\n", offset);
            failures++;
        }
    }

    /* Test 4: field c (enum Color) at offset 4
     * After char a (1 byte), 3 bytes of padding are inserted to align
     * the enum member to its 4-byte alignment boundary.
     * This is the CRITICAL test for enum alignment in struct layout.
     */
    {
        struct S s;
        int offset = (int)((char *)&s.c - (char *)&s);
        if (offset == 4)
            printf("offset_c: OK\n");
        else {
            printf("offset_c: FAIL (offset=%d, expected=4)\n", offset);
            failures++;
        }
    }

    /* Test 5: field b (int) at offset 8
     * After enum c (4 bytes at offset 4), b naturally follows at offset 8.
     * 8 is already a multiple of 4 (int alignment), so no padding needed.
     */
    {
        struct S s;
        int offset = (int)((char *)&s.b - (char *)&s);
        if (offset == 8)
            printf("offset_b: OK\n");
        else {
            printf("offset_b: FAIL (offset=%d, expected=8)\n", offset);
            failures++;
        }
    }

    /* Test 6: enum constants RED=0, GREEN=1, BLUE=2 */
    if (RED == 0 && GREEN == 1 && BLUE == 2)
        printf("enum_values: OK\n");
    else {
        printf("enum_values: FAIL (RED=%d, GREEN=%d, BLUE=%d)\n",
               RED, GREEN, BLUE);
        failures++;
    }

    /* Test 7: values survive noinline function return ABI path
     * Calls make_s with known values and verifies all three fields
     * survive the struct return calling convention for 12-byte struct.
     */
    {
        struct S s = make_s('Z', GREEN, 42);
        if (s.a == 'Z' && s.c == GREEN && s.b == 42)
            printf("store_load: OK\n");
        else {
            printf("store_load: FAIL (a=%d, c=%d, b=%d)\n",
                   s.a, s.c, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct enum member tests passed\n");
    return failures;
}
