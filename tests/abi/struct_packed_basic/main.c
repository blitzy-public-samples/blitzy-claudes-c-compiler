#include <stdio.h>

struct __attribute__((packed)) S {
    char a;
    int b;
    short c;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof is 7 (1 + 4 + 2, no padding due to packed) */
    if (sizeof(struct S) == 7)
        printf("sizeof_packed: OK\n");
    else {
        printf("sizeof_packed: FAIL\n");
        failures++;
    }

    /* Test 2: first field at offset 0 */
    if (offsetof(struct S, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL\n");
        failures++;
    }

    /* Test 3: int b at offset 1 - no padding after char */
    if (offsetof(struct S, b) == 1)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL\n");
        failures++;
    }

    /* Test 4: short c at offset 5 - no padding after int */
    if (offsetof(struct S, c) == 5)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL\n");
        failures++;
    }

    /* Test 5: overall alignment is 1 (packed) */
    if (__alignof__(struct S) == 1)
        printf("alignof_packed: OK\n");
    else {
        printf("alignof_packed: FAIL\n");
        failures++;
    }

    /* Test 6: store and load values through packed struct */
    {
        struct S s;
        s.a = 'A';
        s.b = 12345;
        s.c = 678;
        if (s.a == 'A' && s.b == 12345 && s.c == 678)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All packed basic tests passed\n");
    return failures;
}
