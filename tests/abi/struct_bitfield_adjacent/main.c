#include <stdio.h>

struct S {
    int a:7;
    int b:7;
    int c:7;
    int d:7;
    int e:7;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 8 (two int storage units for 35 bits) */
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

    /* Test 3: all five fields store and load correctly */
    s.a = 10;
    s.b = 20;
    s.c = 30;
    s.d = 40;
    s.e = 50;
    if (s.a == 10 && s.b == 20 && s.c == 30 && s.d == 40 && s.e == 50)
        printf("store_load_all: OK\n");
    else {
        printf("store_load_all: FAIL (a=%d b=%d c=%d d=%d e=%d)\n",
               s.a, s.b, s.c, s.d, s.e);
        failures++;
    }

    /* Test 4: field e in second storage unit works independently */
    s.a = 0; s.b = 0; s.c = 0; s.d = 0;
    s.e = 63;
    if (s.e == 63 && s.a == 0 && s.b == 0 && s.c == 0 && s.d == 0)
        printf("boundary_field_e: OK\n");
    else {
        printf("boundary_field_e: FAIL (a=%d b=%d c=%d d=%d e=%d)\n",
               s.a, s.b, s.c, s.d, s.e);
        failures++;
    }

    /* Test 5: setting all first-unit fields to -1 does not corrupt e */
    s.e = 50;
    s.a = -1;
    s.b = -1;
    s.c = -1;
    s.d = -1;
    if (s.e == 50)
        printf("no_corrupt_across_units: OK\n");
    else {
        printf("no_corrupt_across_units: FAIL (e=%d)\n", s.e);
        failures++;
    }

    /* Test 6: overwriting one field does not corrupt its neighbors */
    s.a = 10;
    s.b = 20;
    s.c = 30;
    s.d = 40;
    s.b = -5;
    if (s.a == 10 && s.c == 30 && s.d == 40)
        printf("adjacent_independence: OK\n");
    else {
        printf("adjacent_independence: FAIL (a=%d c=%d d=%d)\n",
               s.a, s.c, s.d);
        failures++;
    }

    /* Test 7: signed negative values preserved across all fields */
    s.a = -10;
    s.b = -20;
    s.c = -30;
    s.d = -40;
    s.e = -50;
    if (s.a == -10 && s.b == -20 && s.c == -30 && s.d == -40 && s.e == -50)
        printf("negative_values: OK\n");
    else {
        printf("negative_values: FAIL (a=%d b=%d c=%d d=%d e=%d)\n",
               s.a, s.b, s.c, s.d, s.e);
        failures++;
    }

    /* Test 8: min and max values for 7-bit signed fields */
    s.a = -64;
    s.b = 63;
    s.c = -64;
    s.d = 63;
    s.e = -64;
    if (s.a == -64 && s.b == 63 && s.c == -64 && s.d == 63 && s.e == -64)
        printf("extremes: OK\n");
    else {
        printf("extremes: FAIL (a=%d b=%d c=%d d=%d e=%d)\n",
               s.a, s.b, s.c, s.d, s.e);
        failures++;
    }

    if (failures == 0)
        printf("All adjacent bitfield tests passed\n");
    return failures;
}
