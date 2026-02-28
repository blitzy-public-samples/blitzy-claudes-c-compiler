/*
 * ABI integration test: #pragma pack push/pop/reset LIFO stack semantics
 *
 * Verifies that CCC correctly implements #pragma pack(push, N),
 * #pragma pack(pop), and #pragma pack() (reset) with proper LIFO
 * (Last-In-First-Out) stack behavior for the pack alignment value.
 *
 * The preprocessor in src/frontend/preprocessor/pragmas.rs emits
 * synthetic tokens (__ccc_pack_push_N, __ccc_pack_pop, __ccc_pack_reset)
 * which the parser consumes to maintain a pack alignment stack. The
 * struct layout builder in src/common/types.rs (StructLayoutBuilder)
 * uses the current pack alignment as max_field_align when computing
 * field offsets and struct sizes.
 *
 * All five structs use the identical field layout { char a; int b; }
 * so that sizeof differences are directly attributable to the active
 * pack alignment value. The sizeof values are architecture-independent:
 *
 *   Pack State | char a offset | int b offset | sizeof | Reason
 *   -----------+---------------+--------------+--------+-------------------
 *   default    | 0             | 4            | 8      | int natural align=4
 *   pack(2)    | 0             | 2            | 6      | min(4,2)=2 for int
 *   pack(4)    | 0             | 4            | 8      | min(4,4)=4, same
 *   pack(2)    | 0             | 2            | 6      | LIFO restored pack=2
 *   default    | 0             | 4            | 8      | reset restored default
 *
 * These values are identical on x86-64, AArch64, RISC-V 64, and i686
 * because char (size=1, align=1) and int (size=4, align=4) have
 * consistent size and alignment on all four CCC target architectures.
 *
 * Pack Stack State Trace:
 *   Step 0: (initial)       stack=[]           current=default
 *   Step 1: pack(push, 2)   stack=[default]    current=2
 *   Step 2: pack(push, 4)   stack=[default,2]  current=4
 *   Step 3: pack(pop)       stack=[default]    current=2   (LIFO!)
 *   Step 4: pack()          stack=[default]    current=default (reset)
 */

#include <stdio.h>

/* Test 1: Default alignment - before any pragma pack */
struct S_default {
    char a;
    int b;
};

/* Step 1: Push current alignment and set pack=2 */
#pragma pack(push, 2)

/* Test 2: Pack alignment = 2 */
struct S_pack2 {
    char a;
    int b;
};

/* Step 2: Push pack=2 onto stack and set pack=4 */
#pragma pack(push, 4)

/* Test 3: Pack alignment = 4 */
struct S_pack4 {
    char a;
    int b;
};

/* Step 3: Pop - should restore to pack=2 (LIFO) */
#pragma pack(pop)

/* Test 4: Pack alignment should be 2 again (LIFO verified) */
struct S_after_pop {
    char a;
    int b;
};

/* Step 4: Reset to default alignment */
#pragma pack()

/* Test 5: Default alignment restored */
struct S_after_reset {
    char a;
    int b;
};

int main(void) {
    int failures = 0;

    /* Test 1: Default alignment - sizeof should be 8 */
    if (sizeof(struct S_default) == 8)
        printf("default_align: OK\n");
    else {
        printf("default_align: FAIL\n");
        failures++;
    }

    /* Test 2: Pack(2) - sizeof should be 6 */
    if (sizeof(struct S_pack2) == 6)
        printf("pack_push_2: OK\n");
    else {
        printf("pack_push_2: FAIL\n");
        failures++;
    }

    /* Test 3: Pack(4) - sizeof should be 8 (same as default for {char, int}) */
    if (sizeof(struct S_pack4) == 8)
        printf("pack_push_4: OK\n");
    else {
        printf("pack_push_4: FAIL\n");
        failures++;
    }

    /* Test 4: After pop - should be back to pack(2), sizeof should be 6 */
    /* THIS IS THE KEY LIFO VERIFICATION TEST */
    if (sizeof(struct S_after_pop) == 6)
        printf("pop_lifo_to_2: OK\n");
    else {
        printf("pop_lifo_to_2: FAIL\n");
        failures++;
    }

    /* Test 5: After reset - sizeof should be 8 (default alignment) */
    if (sizeof(struct S_after_reset) == 8)
        printf("pack_reset: OK\n");
    else {
        printf("pack_reset: FAIL\n");
        failures++;
    }

    if (failures == 0) printf("All pragma pack tests passed\n");
    return failures;
}
