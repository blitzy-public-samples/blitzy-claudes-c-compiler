#include <stdio.h>

struct S {
    int a:5;
    int :0;
    int b:3;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 8 (zero-width bitfield forces two storage units) */
    if (sizeof(struct S) == 8)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %d)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 2: alignment matches int alignment (4 bytes) */
    if (__alignof__(struct S) == 4)
        printf("alignof_struct: OK\n");
    else {
        printf("alignof_struct: FAIL (got %d)\n", (int)__alignof__(struct S));
        failures++;
    }

    /* Test 3: store and load field a (5-bit signed, value 10) */
    s.a = 10;
    if (s.a == 10)
        printf("store_load_a: OK\n");
    else {
        printf("store_load_a: FAIL (got %d)\n", s.a);
        failures++;
    }

    /* Test 4: store and load field b (3-bit signed, value 3) */
    s.b = 3;
    if (s.b == 3)
        printf("store_load_b: OK\n");
    else {
        printf("store_load_b: FAIL (got %d)\n", s.b);
        failures++;
    }

    /* Test 5: a and b are in separate storage units (zero-width boundary) */
    s.a = 15;
    s.b = -4;
    if (s.a == 15 && s.b == -4)
        printf("zero_width_separates: OK\n");
    else {
        printf("zero_width_separates: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    /* Test 6: writing a does not corrupt b across zero-width boundary */
    s.b = 2;
    s.a = -1;
    if (s.b == 2)
        printf("a_bits_no_leak_to_b: OK\n");
    else {
        printf("a_bits_no_leak_to_b: FAIL (b=%d)\n", s.b);
        failures++;
    }

    /* Test 7: writing b does not corrupt a across zero-width boundary */
    s.a = 10;
    s.b = -1;
    if (s.a == 10)
        printf("b_bits_no_leak_to_a: OK\n");
    else {
        printf("b_bits_no_leak_to_a: FAIL (a=%d)\n", s.a);
        failures++;
    }

    /* Test 8: signed negative values preserved across zero-width boundary */
    s.a = -7;
    s.b = -3;
    if (s.a == -7 && s.b == -3)
        printf("signed_values: OK\n");
    else {
        printf("signed_values: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    if (failures == 0)
        printf("All zero-width bitfield tests passed\n");
    return failures;
}
