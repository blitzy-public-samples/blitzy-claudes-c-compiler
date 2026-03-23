#include <stdio.h>

struct S {
    char a:3;
    char b:5;
    char c:4;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof == 2 (char-sized storage units, not int-sized) */
    if (sizeof(struct S) == 2)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %d)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 2: alignment matches char alignment (1 byte) */
    if (__alignof__(struct S) == 1)
        printf("alignof_struct: OK\n");
    else {
        printf("alignof_struct: FAIL (got %d)\n", (int)__alignof__(struct S));
        failures++;
    }

    /* Test 3: all three fields store and load correctly */
    s.a = 3;
    s.b = 15;
    s.c = 7;
    if (s.a == 3 && s.b == 15 && s.c == 7)
        printf("store_load_all: OK\n");
    else {
        printf("store_load_all: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    /* Test 4: c in second byte is independent from a/b in first byte */
    s.a = 0; s.b = 0;
    s.c = 7;
    if (s.c == 7 && s.a == 0 && s.b == 0)
        printf("byte_boundary: OK\n");
    else {
        printf("byte_boundary: FAIL (a=%d b=%d c=%d)\n", s.a, s.b, s.c);
        failures++;
    }

    /* Test 5: writing a/b in first byte does not corrupt c in second byte */
    s.c = 5;
    s.a = 2;
    s.b = 10;
    if (s.c == 5)
        printf("no_cross_byte_corrupt: OK\n");
    else {
        printf("no_cross_byte_corrupt: FAIL (c=%d)\n", s.c);
        failures++;
    }

    /* Test 6: overwriting a does not corrupt adjacent b in same byte */
    s.a = 1;
    s.b = 12;
    s.a = 3;
    if (s.b == 12)
        printf("adjacent_independence: OK\n");
    else {
        printf("adjacent_independence: FAIL (b=%d)\n", s.b);
        failures++;
    }

    if (failures == 0)
        printf("All char bitfield tests passed\n");
    return failures;
}
