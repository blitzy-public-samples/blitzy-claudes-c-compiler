#include <stdio.h>

struct S {
    int a:3;
    int b:5;
    int c:17;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 4 (all 25 bits fit in one int storage unit) */
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

    /* Test 3: all three fields store and load correctly */
    s.a = 3;
    s.b = 15;
    s.c = 1000;
    if (s.a == 3 && s.b == 15 && s.c == 1000)
        printf("store_load_all: OK\n");
    else {
        printf("store_load_all: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    /* Test 4: all fields pack in one unit — setting c does not corrupt a/b */
    s.a = 0;
    s.b = 0;
    s.c = 65535;
    if (s.c == 65535 && s.a == 0 && s.b == 0)
        printf("single_unit_packing: OK\n");
    else {
        printf("single_unit_packing: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    /* Test 5: overwriting one field does not corrupt its neighbors */
    s.a = 2;
    s.b = 10;
    s.c = 500;
    s.b = -5;
    if (s.a == 2 && s.c == 500)
        printf("field_independence: OK\n");
    else {
        printf("field_independence: FAIL (a=%d c=%d)\n", s.a, s.c);
        failures++;
    }

    /* Test 6: signed negative values preserved across all fields */
    s.a = -4;
    s.b = -16;
    s.c = -1000;
    if (s.a == -4 && s.b == -16 && s.c == -1000)
        printf("negative_values: OK\n");
    else {
        printf("negative_values: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    /* Test 7: min and max values for each bit width */
    s.a = -4;
    s.b = 15;
    s.c = -65536;
    if (s.a == -4 && s.b == 15 && s.c == -65536)
        printf("extremes: OK\n");
    else {
        printf("extremes: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    if (failures == 0)
        printf("All basic int bitfield tests passed\n");
    return failures;
}
