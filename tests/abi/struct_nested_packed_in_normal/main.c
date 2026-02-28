#include <stdio.h>
#include <stddef.h>

/* Inner struct is packed: sizeof = 5, alignment = 1 */
struct __attribute__((packed)) Inner {
    char a;
    int b;
};

/* Outer struct is NOT packed - packed does NOT propagate from Inner */
struct Outer {
    struct Inner x;
    int y;
};

int main(void) {
    int failures = 0;

    /* Test 1: Inner is packed - sizeof should be 5 (1 + 4, no padding) */
    if (sizeof(struct Inner) == 5)
        printf("sizeof_inner: OK\n");
    else {
        printf("sizeof_inner: FAIL\n");
        failures++;
    }

    /* Test 2: Inner alignment is 1 (packed) */
    if (__alignof__(struct Inner) == 1)
        printf("alignof_inner: OK\n");
    else {
        printf("alignof_inner: FAIL\n");
        failures++;
    }

    /* Test 3: Inner.b at offset 1 (packed, no padding between char and int) */
    if (offsetof(struct Inner, b) == 1)
        printf("offsetof_inner_b: OK\n");
    else {
        printf("offsetof_inner_b: FAIL\n");
        failures++;
    }

    /* Test 4: Outer sizeof is 12 - packed does NOT propagate to outer */
    if (sizeof(struct Outer) == 12)
        printf("sizeof_outer: OK\n");
    else {
        printf("sizeof_outer: FAIL\n");
        failures++;
    }

    /* Test 5: Outer alignment is 4 (from int y, not 1 from Inner) */
    if (__alignof__(struct Outer) == 4)
        printf("alignof_outer: OK\n");
    else {
        printf("alignof_outer: FAIL\n");
        failures++;
    }

    /* Test 6: Outer.y at offset 8 (int naturally aligned, 3 bytes padding) */
    if (offsetof(struct Outer, y) == 8)
        printf("offsetof_outer_y: OK\n");
    else {
        printf("offsetof_outer_y: FAIL\n");
        failures++;
    }

    /* Test 7: Outer.x at offset 0 */
    if (offsetof(struct Outer, x) == 0)
        printf("offsetof_outer_x: OK\n");
    else {
        printf("offsetof_outer_x: FAIL\n");
        failures++;
    }

    /* Test 8: Value correctness through nested packed struct */
    {
        struct Outer o;
        o.x.a = 'Z';
        o.x.b = 99999;
        o.y = 42;
        if (o.x.a == 'Z' && o.x.b == 99999 && o.y == 42)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All nested packed in normal tests passed\n");
    return failures;
}
