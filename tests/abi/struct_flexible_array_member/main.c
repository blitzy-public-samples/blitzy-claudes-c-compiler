#include <stdio.h>

struct S {
    int len;
    char data[];
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof is 4 (FAM contributes 0 to struct size) */
    if (sizeof(struct S) == 4)
        printf("sizeof_fam: OK\n");
    else {
        printf("sizeof_fam: FAIL (got %d)\n", (int)sizeof(struct S));
        failures++;
    }

    /* Test 2: FAM data[] starts at offset 4 (immediately after int len) */
    if (offsetof(struct S, data) == 4)
        printf("offsetof_data: OK\n");
    else {
        printf("offsetof_data: FAIL (got %d)\n", (int)offsetof(struct S, data));
        failures++;
    }

    if (failures == 0)
        printf("All flexible array member tests passed\n");
    return failures;
}
