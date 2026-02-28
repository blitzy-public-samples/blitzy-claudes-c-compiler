#include <stdio.h>
#include <stddef.h>

struct __attribute__((packed)) S {
    int a:3;
    int b:12;
    char c;
    int d:7;
};

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof is 4 (packed contiguous bit-stream) */
    if (sizeof(struct S) == 4)
        printf("sizeof_packed_bf: OK\n");
    else {
        printf("sizeof_packed_bf: FAIL (got %d)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 2: alignment is 1 (packed caps all alignments) */
    if (__alignof__(struct S) == 1)
        printf("alignof_packed_bf: OK\n");
    else {
        printf("alignof_packed_bf: FAIL (got %d)\n", (int)__alignof__(struct S));
        failures++;
    }

    /* Test 3: char c at offset 2 (after 15 bits of a+b, ceil(15/8)=2) */
    if (offsetof(struct S, c) == 2)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL (got %d)\n", (int)offsetof(struct S, c));
        failures++;
    }

    /* Test 4: all four fields store and load correctly */
    s.a = 2;
    s.b = 1000;
    s.c = 'Z';
    s.d = 50;
    if (s.a == 2 && s.b == 1000 && s.c == 'Z' && s.d == 50)
        printf("store_load_all: OK\n");
    else {
        printf("store_load_all: FAIL (a=%d b=%d c=%c d=%d)\n", s.a, s.b, s.c, s.d);
        failures++;
    }

    /* Test 5: negative values preserved in bitfields */
    s.a = -4;
    s.b = -2048;
    s.c = 'A';
    s.d = -64;
    if (s.a == -4 && s.b == -2048 && s.c == 'A' && s.d == -64)
        printf("negative_values: OK\n");
    else {
        printf("negative_values: FAIL (a=%d b=%d c=%c d=%d)\n", s.a, s.b, s.c, s.d);
        failures++;
    }

    /* Test 6: writing char c does not corrupt bitfields */
    s.a = 2;
    s.b = 1000;
    s.d = 30;
    s.c = 'X';
    if (s.a == 2 && s.b == 1000 && s.d == 30)
        printf("c_no_corrupt: OK\n");
    else {
        printf("c_no_corrupt: FAIL (a=%d b=%d d=%d)\n", s.a, s.b, s.d);
        failures++;
    }

    /* Test 7: writing bitfield a does not corrupt b, c, d */
    s.b = 500;
    s.c = 'M';
    s.d = 20;
    s.a = -3;
    if (s.b == 500 && s.c == 'M' && s.d == 20)
        printf("a_no_corrupt: OK\n");
    else {
        printf("a_no_corrupt: FAIL (b=%d c=%c d=%d)\n", s.b, s.c, s.d);
        failures++;
    }

    /* Test 8: extreme min/max boundary values */
    s.a = -4;
    s.b = 2047;
    s.c = 127;
    s.d = 63;
    if (s.a == -4 && s.b == 2047 && s.c == 127 && s.d == 63)
        printf("extremes: OK\n");
    else {
        printf("extremes: FAIL (a=%d b=%d c=%d d=%d)\n", s.a, s.b, (int)s.c, s.d);
        failures++;
    }

    if (failures == 0)
        printf("All packed bitfield tests passed\n");
    return failures;
}
