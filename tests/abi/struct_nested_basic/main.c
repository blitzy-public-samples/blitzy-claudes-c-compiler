/*
 * ABI integration test: nested struct layout
 *
 * Verifies that CCC correctly computes layout for nested structs.
 * struct Inner { int x; char y; } nested inside
 * struct Outer { char a; struct Inner b; int c; }
 * must have correct sizeof, offsetof, and alignment propagation.
 *
 * The inner struct's alignment (4, from its int member) must propagate
 * to force padding in the outer struct.
 *
 * Layout analysis (architecture-independent, int=4/4, char=1/1 on all targets):
 *
 * struct Inner:
 *   x (int):  offset=0, size=4, align=4
 *   y (char): offset=4, size=1, align=1
 *   trailing padding: 3 bytes
 *   sizeof=8, alignof=4
 *
 * struct Outer:
 *   a (char):         offset=0,  size=1, align=1
 *   padding:          3 bytes (to align b to 4)
 *   b (struct Inner): offset=4,  size=8, align=4
 *   c (int):          offset=12, size=4, align=4
 *   sizeof=16, alignof=4
 */

#include <stdio.h>

struct Inner {
    int x;
    char y;
};

struct Outer {
    char a;
    struct Inner b;
    int c;
};

__attribute__((noinline))
struct Outer make_outer(char a, int x, char y, int c) {
    struct Outer o;
    o.a = a;
    o.b.x = x;
    o.b.y = y;
    o.c = c;
    return o;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct Inner) == 8
     * int (4) + char (1) + 3 bytes trailing padding = 8
     */
    if (sizeof(struct Inner) == 8)
        printf("sizeof_Inner: OK\n");
    else {
        printf("sizeof_Inner: FAIL (got %d)\n", (int)sizeof(struct Inner));
        failures++;
    }

    /* Test 2: sizeof(struct Outer) == 16
     * char (1) + 3 pad + Inner (8) + int (4) = 16
     */
    if (sizeof(struct Outer) == 16)
        printf("sizeof_Outer: OK\n");
    else {
        printf("sizeof_Outer: FAIL (got %d)\n", (int)sizeof(struct Outer));
        failures++;
    }

    /* Test 3: offsetof(struct Outer, a) == 0
     * First field always at offset 0.
     */
    if (offsetof(struct Outer, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n", (int)offsetof(struct Outer, a));
        failures++;
    }

    /* Test 4: offsetof(struct Outer, b) == 4 — KEY ASSERTION
     * Inner struct's alignment (4, from int x) forces 3 bytes of
     * padding after char a, so b starts at offset 4.
     */
    if (offsetof(struct Outer, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d)\n", (int)offsetof(struct Outer, b));
        failures++;
    }

    /* Test 5: offsetof(struct Outer, c) == 12 — KEY ASSERTION
     * c comes after Inner b (at offset 4+8=12), already aligned
     * to int's alignment of 4.
     */
    if (offsetof(struct Outer, c) == 12)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL (got %d)\n", (int)offsetof(struct Outer, c));
        failures++;
    }

    /* Test 6: offsetof(struct Inner, x) == 0
     * First field of Inner is always at offset 0.
     */
    if (offsetof(struct Inner, x) == 0)
        printf("offsetof_inner_x: OK\n");
    else {
        printf("offsetof_inner_x: FAIL (got %d)\n", (int)offsetof(struct Inner, x));
        failures++;
    }

    /* Test 7: offsetof(struct Inner, y) == 4
     * char y follows int x (size 4), at offset 4.
     */
    if (offsetof(struct Inner, y) == 4)
        printf("offsetof_inner_y: OK\n");
    else {
        printf("offsetof_inner_y: FAIL (got %d)\n", (int)offsetof(struct Inner, y));
        failures++;
    }

    /* Test 8: __alignof__(struct Inner) == 4
     * Inner's alignment is determined by its most-aligned member (int, align=4).
     */
    if (__alignof__(struct Inner) == 4)
        printf("alignof_Inner: OK\n");
    else {
        printf("alignof_Inner: FAIL (got %d)\n", (int)__alignof__(struct Inner));
        failures++;
    }

    /* Test 9: __alignof__(struct Outer) == 4
     * Outer's alignment is max of members: Inner align=4, int c align=4, char a align=1.
     * Result: 4.
     */
    if (__alignof__(struct Outer) == 4)
        printf("alignof_Outer: OK\n");
    else {
        printf("alignof_Outer: FAIL (got %d)\n", (int)__alignof__(struct Outer));
        failures++;
    }

    /* Test 10: Value correctness through nested struct via noinline return
     * Verifies all four field values survive the struct return ABI path.
     */
    {
        struct Outer o = make_outer('Z', 42, 'W', 99);
        if (o.a == 'Z' && o.b.x == 42 && o.b.y == 'W' && o.c == 99)
            printf("value_nested: OK\n");
        else {
            printf("value_nested: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All nested basic tests passed\n");
    return failures;
}
