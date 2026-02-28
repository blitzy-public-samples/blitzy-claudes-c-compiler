#include <stdio.h>

/* Inner struct is packed: sizeof = 5, alignment = 1 */
struct __attribute__((packed)) Inner {
    char a;
    int b;
};

/* Outer struct is NOT packed but contains packed Inner */
struct Outer {
    char x;
    struct Inner y;
};

/* OuterWithInt shows that non-packed outer still pads around int */
struct OuterWithInt {
    char x;
    struct Inner y;
    int z;
};

int main(void) {
    int failures = 0;

    /* Test 1: Inner is packed - sizeof should be 5 (1 + 4, no padding) */
    if (sizeof(struct Inner) == 5)
        printf("sizeof_inner_packed: OK\n");
    else {
        printf("sizeof_inner_packed: FAIL\n");
        failures++;
    }

    /* Test 2: Inner.b at offset 1 (packed, no padding between char and int) */
    if (offsetof(struct Inner, b) == 1)
        printf("offsetof_inner_b: OK\n");
    else {
        printf("offsetof_inner_b: FAIL\n");
        failures++;
    }

    /* Test 3: Outer sizeof is 6 (char=1 + Inner=5, both align 1, no padding) */
    if (sizeof(struct Outer) == 6)
        printf("sizeof_outer: OK\n");
    else {
        printf("sizeof_outer: FAIL\n");
        failures++;
    }

    /* Test 4: Outer.y at offset 1 (Inner alignment is 1, no padding) */
    if (offsetof(struct Outer, y) == 1)
        printf("offsetof_outer_y: OK\n");
    else {
        printf("offsetof_outer_y: FAIL\n");
        failures++;
    }

    /* Test 5: OuterWithInt sizeof is 12 - outer pads for int z alignment */
    if (sizeof(struct OuterWithInt) == 12)
        printf("sizeof_outer_with_int: OK\n");
    else {
        printf("sizeof_outer_with_int: FAIL\n");
        failures++;
    }

    /* Test 6: OuterWithInt.z at offset 8 (int naturally aligned to 4) */
    if (offsetof(struct OuterWithInt, z) == 8)
        printf("offsetof_outer_z: OK\n");
    else {
        printf("offsetof_outer_z: FAIL\n");
        failures++;
    }

    /* Test 7: Value correctness through nested packed struct */
    {
        struct Outer s;
        s.x = 'A';
        s.y.a = 'B';
        s.y.b = 42;
        if (s.x == 'A' && s.y.a == 'B' && s.y.b == 42)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0) printf("All packed nested tests passed\n");
    return failures;
}
