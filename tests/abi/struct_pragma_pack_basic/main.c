#include <stdio.h>

/* Test 1: Default alignment - before any pragma pack */
struct S_default {
    char a;
    int b;
    double c;
};

/* Step 1: Push current alignment and set pack=1 */
#pragma pack(push, 1)

/* Test 2: Pack alignment = 1 (no padding at all) */
struct S_packed {
    char a;
    int b;
    double c;
};

/* Step 2: Pop - restore default alignment */
#pragma pack(pop)

/* Test 3: Default alignment restored after pop */
struct S_restored {
    char a;
    int b;
    double c;
};

int main(void) {
    int failures = 0;

    /* Test 1: Default alignment - sizeof should be 16 */
    if (sizeof(struct S_default) == 16)
        printf("default_align: OK\n");
    else {
        printf("default_align: FAIL\n");
        failures++;
    }

    /* Test 2: Pack(1) - sizeof should be 13 (no padding) */
    if (sizeof(struct S_packed) == 13)
        printf("pack1_no_padding: OK\n");
    else {
        printf("pack1_no_padding: FAIL\n");
        failures++;
    }

    /* Test 3: After pop - default restored, sizeof should be 16 */
    if (sizeof(struct S_restored) == 16)
        printf("restored_default: OK\n");
    else {
        printf("restored_default: FAIL\n");
        failures++;
    }

    if (failures == 0) printf("All pragma pack basic tests passed\n");
    return failures;
}
