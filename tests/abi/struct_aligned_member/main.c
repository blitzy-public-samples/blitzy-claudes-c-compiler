/*
 * ABI integration test: per-member __attribute__((aligned(N))) behavior
 *
 * Verifies that an explicit alignment attribute on an individual struct member
 * correctly overrides the default natural alignment, affecting member offset
 * placement and overall struct size.
 *
 * This exercises the layout logic in src/common/types.rs, specifically
 * StructLayoutBuilder::compute_field_alignment() where the priority for a
 * field with explicit alignment is:
 *     natural_align.max(explicit)
 *
 * Struct under test:
 *   struct S {
 *       char a;                                  // offset 0, size 1
 *       int b __attribute__((aligned(16)));       // offset 16, size 4
 *       char c;                                  // offset 20, size 1
 *   };
 *
 * Layout analysis (all 4 architectures: x86-64, AArch64, RISC-V 64, i686):
 *   - Field a (char): align=1, offset=0, size=1. After: offset=1.
 *   - Field b (int, aligned(16)): field_align=max(4,16)=16.
 *     offset=align_up(1,16)=16, size=4. After: offset=20.
 *   - Field c (char): align=1, offset=20, size=1. After: offset=21.
 *   - Finalize: max_align=16, size=align_up(21,16)=32.
 *   => sizeof=32, alignof=16
 *   => offsetof(a)=0, offsetof(b)=16, offsetof(c)=20
 *
 * Baseline comparison struct (no aligned attribute):
 *   struct S_default { char a; int b; char c; };
 *   => sizeof=12, offsetof(a)=0, offsetof(b)=4, offsetof(c)=8
 *
 * All sizeof and offsetof values are architecture-independent because char=1
 * and int=4 on all 4 CCC targets.
 *
 * No expected.skip.* files needed — test works on all architectures.
 */

#include <stdio.h>
#include <stddef.h>

/* Struct with per-member aligned attribute — THE KEY TEST */
struct S {
    char a;
    int b __attribute__((aligned(16)));
    char c;
};

/* Baseline struct without alignment override — for comparison */
struct S_default {
    char a;
    int b;
    char c;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof aligned-member struct = 32 */
    if (sizeof(struct S) == 32)
        printf("sizeof_aligned_member: OK\n");
    else {
        printf("sizeof_aligned_member: FAIL (got %zu)\n", sizeof(struct S));
        failures++;
    }

    /* Test 2: offsetof(b) = 16 — THE KEY ASSERTION
     * __attribute__((aligned(16))) on member b forces 16-byte alignment
     * instead of the natural int alignment of 4. */
    if (offsetof(struct S, b) == 16)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %zu)\n", offsetof(struct S, b));
        failures++;
    }

    /* Test 3: offsetof(a) = 0 — first field always at offset 0 */
    if (offsetof(struct S, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %zu)\n", offsetof(struct S, a));
        failures++;
    }

    /* Test 4: offsetof(c) = 20 — immediately after b (16 + sizeof(int) = 20) */
    if (offsetof(struct S, c) == 20)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL (got %zu)\n", offsetof(struct S, c));
        failures++;
    }

    /* Test 5: sizeof default struct = 12 — baseline comparison */
    if (sizeof(struct S_default) == 12)
        printf("sizeof_default: OK\n");
    else {
        printf("sizeof_default: FAIL (got %zu)\n", sizeof(struct S_default));
        failures++;
    }

    /* Test 6: offsetof(b) in default struct = 4 — natural int alignment */
    if (offsetof(struct S_default, b) == 4)
        printf("offsetof_b_default: OK\n");
    else {
        printf("offsetof_b_default: FAIL (got %zu)\n", offsetof(struct S_default, b));
        failures++;
    }

    /* Test 7: Value correctness through aligned-member struct */
    {
        struct S s;
        s.a = 'X';
        s.b = 12345678;
        s.c = 'Y';
        if (s.a == 'X' && s.b == 12345678 && s.c == 'Y')
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    /* Summary */
    if (failures == 0)
        printf("All aligned member tests passed\n");

    return failures;
}
