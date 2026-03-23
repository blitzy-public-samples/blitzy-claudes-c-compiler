#include <stdio.h>

struct S {
    int a:5;
    int :3;
    int b:7;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 4 (5+3+7=15 bits fit in one int storage unit) */
    if (sizeof(struct S) == 4)
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

    /* Test 3: store and load field a (5-bit signed, value 13) */
    s.a = 13;
    if (s.a == 13)
        printf("store_load_a: OK\n");
    else {
        printf("store_load_a: FAIL (got %d)\n", s.a);
        failures++;
    }

    /* Test 4: store and load field b (7-bit signed, value 42) */
    s.b = 42;
    if (s.b == 42)
        printf("store_load_b: OK\n");
    else {
        printf("store_load_b: FAIL (got %d)\n", s.b);
        failures++;
    }

    /* Test 5: field a retains value after field b is written */
    s.a = 13;
    s.b = 42;
    if (s.a == 13)
        printf("a_stable_after_b: OK\n");
    else {
        printf("a_stable_after_b: FAIL (got %d)\n", s.a);
        failures++;
    }

    /* Test 6: field b retains value after field a is written */
    s.b = 42;
    s.a = -7;
    if (s.b == 42)
        printf("b_stable_after_a: OK\n");
    else {
        printf("b_stable_after_a: FAIL (got %d)\n", s.b);
        failures++;
    }

    /* Test 7: setting all a bits does not leak into b across unnamed gap */
    s.a = -1;
    s.b = 0;
    if (s.b == 0)
        printf("unnamed_gap_no_leak: OK\n");
    else {
        printf("unnamed_gap_no_leak: FAIL (got %d)\n", s.b);
        failures++;
    }

    /* Test 8: signed negative values preserved through unnamed bitfield gap */
    s.a = -7;
    s.b = -30;
    if (s.a == -7 && s.b == -30)
        printf("signed_values: OK\n");
    else {
        printf("signed_values: FAIL (a=%d, b=%d)\n", s.a, s.b);
        failures++;
    }

    if (failures == 0)
        printf("All unnamed bitfield tests passed\n");
    return failures;
}
