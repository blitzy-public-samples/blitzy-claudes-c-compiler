/*
 * ABI Integration Test: Struct Containing an Array of Structs
 *
 * Verifies that CCC correctly implements the layout of a struct that
 * contains an array of structs across all four target architectures
 * (x86-64, AArch64, RISC-V 64, i686).
 *
 * Tests validate:
 *   1. sizeof(struct Elem) == 8  (short:2 + pad:2 + int:4)
 *   2. alignof(struct Elem) == 4  (max(alignof(short), alignof(int)))
 *   3. Field offsets within struct Elem (x at 0, y at 4)
 *   4. Array stride of items[] equals sizeof(struct Elem) == 8
 *   5. sizeof(struct S) == 36  (items:32 + tag:1 + pad:3)
 *   6. Field offsets within struct S (items at 0, tag at 32)
 *   7. Store/load correctness for all array elements
 *   8. Return-by-value ABI for 36-byte struct (sret on all architectures)
 *
 * Layout is identical on all four architectures because short=2/align=2
 * and int=4/align=4 are the same on all CCC targets.
 */

#include <stdio.h>

struct Elem { short x; int y; };
struct S { struct Elem items[4]; char tag; };

__attribute__((noinline))
static struct S make_s(short x0, int y0, short x1, int y1,
                       short x2, int y2, short x3, int y3, char t) {
    struct S s;
    s.items[0].x = x0; s.items[0].y = y0;
    s.items[1].x = x1; s.items[1].y = y1;
    s.items[2].x = x2; s.items[2].y = y2;
    s.items[3].x = x3; s.items[3].y = y3;
    s.tag = t;
    return s;
}

int main(void) {
    int failures = 0;
    struct Elem e;
    struct S s;

    /* Test 1: sizeof(struct Elem) == 8 */
    if ((int)sizeof(struct Elem) == 8)
        printf("sizeof_elem: OK\n");
    else {
        printf("sizeof_elem: FAIL (got %d, expected 8)\n", (int)sizeof(struct Elem));
        failures++;
    }

    /* Test 2: alignof(struct Elem) == 4 via AlignHelper technique */
    {
        struct AlignHelper { char c; struct Elem e; };
        struct AlignHelper ah;
        int elem_align = (int)((char *)&ah.e - (char *)&ah);
        if (elem_align == 4)
            printf("alignof_elem: OK\n");
        else {
            printf("alignof_elem: FAIL (got %d, expected 4)\n", elem_align);
            failures++;
        }
    }

    /* Test 3: short x at offset 0 within struct Elem */
    {
        int off = (int)((char *)&e.x - (char *)&e);
        if (off == 0)
            printf("elem_offset_x: OK\n");
        else {
            printf("elem_offset_x: FAIL (offset=%d, expected 0)\n", off);
            failures++;
        }
    }

    /* Test 4: int y at offset 4 within struct Elem */
    {
        int off = (int)((char *)&e.y - (char *)&e);
        if (off == 4)
            printf("elem_offset_y: OK\n");
        else {
            printf("elem_offset_y: FAIL (offset=%d, expected 4)\n", off);
            failures++;
        }
    }

    /* Test 5: array stride of items[] equals sizeof(struct Elem) == 8 */
    {
        int stride = (int)((char *)&s.items[1] - (char *)&s.items[0]);
        if (stride == 8)
            printf("array_stride: OK\n");
        else {
            printf("array_stride: FAIL (stride=%d, expected 8)\n", stride);
            failures++;
        }
    }

    /* Test 6: sizeof(struct S) == 36 */
    if ((int)sizeof(struct S) == 36)
        printf("sizeof_s: OK\n");
    else {
        printf("sizeof_s: FAIL (got %d, expected 36)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 7: items at offset 0 within struct S */
    {
        int off = (int)((char *)&s.items[0] - (char *)&s);
        if (off == 0)
            printf("offset_items: OK\n");
        else {
            printf("offset_items: FAIL (offset=%d, expected 0)\n", off);
            failures++;
        }
    }

    /* Test 8: tag at offset 32 within struct S */
    {
        int off = (int)((char *)&s.tag - (char *)&s);
        if (off == 32)
            printf("offset_tag: OK\n");
        else {
            printf("offset_tag: FAIL (offset=%d, expected 32)\n", off);
            failures++;
        }
    }

    /* Test 9: store values in all 4 elements and tag, read back */
    {
        s.items[0].x = 10; s.items[0].y = 100;
        s.items[1].x = 20; s.items[1].y = 200;
        s.items[2].x = 30; s.items[2].y = 300;
        s.items[3].x = 40; s.items[3].y = 400;
        s.tag = 'T';
        if (s.items[0].x == 10 && s.items[0].y == 100 &&
            s.items[1].x == 20 && s.items[1].y == 200 &&
            s.items[2].x == 30 && s.items[2].y == 300 &&
            s.items[3].x == 40 && s.items[3].y == 400 &&
            s.tag == 'T')
            printf("store_load_array: OK\n");
        else {
            printf("store_load_array: FAIL\n");
            failures++;
        }
    }

    /* Test 10: return struct S through ABI via noinline function */
    {
        struct S r = make_s(1, 11, 2, 22, 3, 33, 4, 44, 'R');
        if (r.items[0].x == 1 && r.items[0].y == 11 &&
            r.items[1].x == 2 && r.items[1].y == 22 &&
            r.items[2].x == 3 && r.items[2].y == 33 &&
            r.items[3].x == 4 && r.items[3].y == 44 &&
            r.tag == 'R')
            printf("return_struct: OK\n");
        else {
            printf("return_struct: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct array of struct tests passed\n");
    return failures;
}
