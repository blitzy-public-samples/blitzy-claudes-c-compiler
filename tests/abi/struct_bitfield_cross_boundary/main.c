#include <stdio.h>

struct S {
    int a:30;
    int b:10;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 8 (b crosses boundary, needs new storage unit) */
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

    /* Test 3: both fields store and load correctly */
    s.a = 100000;
    s.b = 200;
    if (s.a == 100000 && s.b == 200)
        printf("store_load_both: OK\n");
    else {
        printf("store_load_both: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    /* Test 4: field b in second storage unit works independently */
    s.a = 0;
    s.b = 511;
    if (s.b == 511 && s.a == 0)
        printf("boundary_field_b: OK\n");
    else {
        printf("boundary_field_b: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    /* Test 5: writing a does not corrupt b across boundary */
    s.b = 200;
    s.a = -1;
    if (s.b == 200)
        printf("cross_unit_no_corrupt: OK\n");
    else {
        printf("cross_unit_no_corrupt: FAIL (b=%d)\n", s.b);
        failures++;
    }

    /* Test 6: writing b does not corrupt a across boundary */
    s.a = 100000;
    s.b = -1;
    if (s.a == 100000)
        printf("reverse_no_corrupt: OK\n");
    else {
        printf("reverse_no_corrupt: FAIL (a=%d)\n", s.a);
        failures++;
    }

    /* Test 7: signed negative values preserved across boundary */
    s.a = -100000;
    s.b = -200;
    if (s.a == -100000 && s.b == -200)
        printf("negative_values: OK\n");
    else {
        printf("negative_values: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    /* Test 8: extreme min/max values for both fields */
    s.a = -536870912;
    s.b = 511;
    if (s.a == -536870912 && s.b == 511)
        printf("extremes: OK\n");
    else {
        printf("extremes: FAIL (a=%d b=%d)\n", s.a, s.b);
        failures++;
    }

    if (failures == 0)
        printf("All cross-boundary bitfield tests passed\n");
    return failures;
}
