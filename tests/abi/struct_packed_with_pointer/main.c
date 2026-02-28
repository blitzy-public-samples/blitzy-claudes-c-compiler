#include <stdio.h>

struct __attribute__((packed)) S {
    char a;
    void *p;
    int b;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof is sum of field sizes (no padding due to packed) */
    if (sizeof(struct S) == 1 + sizeof(void *) + sizeof(int))
        printf("sizeof_packed_ptr_struct: OK\n");
    else {
        printf("sizeof_packed_ptr_struct: FAIL\n");
        failures++;
    }

    /* Test 2: KEY TEST - pointer at offset 1, no alignment padding */
    if (offsetof(struct S, p) == 1)
        printf("offset_p_is_one: OK\n");
    else {
        printf("offset_p_is_one: FAIL\n");
        failures++;
    }

    /* Test 3: int b follows immediately after pointer */
    if (offsetof(struct S, b) == 1 + (int)sizeof(void *))
        printf("offset_b_after_ptr: OK\n");
    else {
        printf("offset_b_after_ptr: FAIL\n");
        failures++;
    }

    /* Test 4: Overall struct alignment is 1 (packed) */
    if (__alignof__(struct S) == 1)
        printf("alignof_packed_struct: OK\n");
    else {
        printf("alignof_packed_struct: FAIL\n");
        failures++;
    }

    /* Test 5: Store and load values through packed struct */
    {
        int val = 99;
        struct S s;
        s.a = 'X';
        s.p = &val;
        s.b = 42;
        if (s.a == 'X' && s.b == 42)
            printf("value_store_load: OK\n");
        else {
            printf("value_store_load: FAIL\n");
            failures++;
        }
    }

    /* Test 6: Pointer dereference through packed struct member */
    {
        int val = 99;
        struct S s;
        s.a = 'A';
        s.p = &val;
        s.b = 77;
        if (*(int *)s.p == 99)
            printf("pointer_value_survives: OK\n");
        else {
            printf("pointer_value_survives: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All packed pointer struct tests passed\n");
    return failures;
}
