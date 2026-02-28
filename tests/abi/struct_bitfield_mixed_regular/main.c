#include <stdio.h>

struct S {
    int a:5;
    char b;
    int c:11;
    double d;
};

#ifdef __i386__
#define EXPECTED_SIZEOF 12
#define EXPECTED_ALIGNOF 4
#define EXPECTED_OFFSET_D 4
#else
#define EXPECTED_SIZEOF 16
#define EXPECTED_ALIGNOF 8
#define EXPECTED_OFFSET_D 8
#endif

int main(void) {
    int failures = 0;
    struct S s;

    /* Test 1: sizeof(struct S) */
    if (sizeof(struct S) == EXPECTED_SIZEOF)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct S), EXPECTED_SIZEOF);
        failures++;
    }

    /* Test 2: alignof(struct S) */
    if (__alignof__(struct S) == EXPECTED_ALIGNOF)
        printf("alignof_struct: OK\n");
    else {
        printf("alignof_struct: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct S), EXPECTED_ALIGNOF);
        failures++;
    }

    /* Test 3: offsetof(b) == 1 — regular char placed right after bitfield used bits */
    {
        struct S tmp;
        int off_b = (int)((char *)&tmp.b - (char *)&tmp);
        if (off_b == 1)
            printf("offset_b: OK\n");
        else {
            printf("offset_b: FAIL (got %d)\n", off_b);
            failures++;
        }
    }

    /* Test 4: offsetof(d) — architecture-dependent */
    {
        struct S tmp;
        int off_d = (int)((char *)&tmp.d - (char *)&tmp);
        if (off_d == EXPECTED_OFFSET_D)
            printf("offset_d: OK\n");
        else {
            printf("offset_d: FAIL (got %d, expected %d)\n", off_d, EXPECTED_OFFSET_D);
            failures++;
        }
    }

    /* Test 5: store/load bitfield a (5-bit signed, value 13) */
    s.a = 13;
    if (s.a == 13)
        printf("store_load_a: OK\n");
    else {
        printf("store_load_a: FAIL (got %d)\n", s.a);
        failures++;
    }

    /* Test 6: store/load regular char b */
    s.b = 'X';
    if (s.b == 'X')
        printf("store_load_b: OK\n");
    else {
        printf("store_load_b: FAIL (got %d)\n", (int)s.b);
        failures++;
    }

    /* Test 7: store/load bitfield c (11-bit signed, value 1000) */
    s.c = 1000;
    if (s.c == 1000)
        printf("store_load_c: OK\n");
    else {
        printf("store_load_c: FAIL (got %d)\n", s.c);
        failures++;
    }

    /* Test 8: store/load regular double d */
    s.d = 3.14;
    if (s.d == 3.14)
        printf("store_load_d: OK\n");
    else {
        printf("store_load_d: FAIL\n");
        failures++;
    }

    /* Test 9: writing regular b preserves bitfields a and c */
    s.a = -7;
    s.c = -500;
    s.b = 'Z';
    if (s.a == -7 && s.c == -500)
        printf("b_no_corrupt_bitfields: OK\n");
    else {
        printf("b_no_corrupt_bitfields: FAIL (a=%d, c=%d)\n", s.a, s.c);
        failures++;
    }

    /* Test 10: writing bitfield a preserves regular b, bitfield c, regular d */
    s.b = 'A';
    s.c = 100;
    s.d = 2.718;
    s.a = 15;
    if (s.b == 'A' && s.c == 100 && s.d == 2.718)
        printf("a_no_corrupt_others: OK\n");
    else {
        printf("a_no_corrupt_others: FAIL (b=%c, c=%d)\n", s.b, s.c);
        failures++;
    }

    if (failures == 0)
        printf("All bitfield mixed regular tests passed\n");
    return failures;
}
