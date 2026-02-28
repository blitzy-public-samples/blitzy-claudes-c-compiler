/*
 * ABI integration test: __attribute__((aligned(16))) at the struct level
 *
 * Verifies that struct-level aligned(16) correctly:
 *   1. Raises the struct's overall alignment to 16
 *   2. Rounds the struct size up to the nearest 16-byte boundary
 *   3. Does NOT change individual field offsets (normal layout rules apply)
 *   4. Allows correct value store/load through the aligned struct
 *
 * Layout analysis for S_aligned16 { int a; int b; }:
 *   - Field a (int):  align=4, offset=0, size=4  -> cursor=4, max_align=4
 *   - Field b (int):  align=4, offset=4, size=4  -> cursor=8, max_align=4
 *   - Natural finalize: size=align_up(8,4)=8, align=4
 *   - Struct-level aligned(16): 16 > 4, so align=16, size=(8+15)&~15=16
 *   => sizeof=16, alignof=16, offsets: a=0, b=4
 *
 * Layout analysis for S_larger { int arr[5]; }:
 *   - Field arr (int[5]): align=4, offset=0, size=20 -> cursor=20, max_align=4
 *   - Natural finalize: size=align_up(20,4)=20, align=4
 *   - Struct-level aligned(16): 16 > 4, so align=16, size=(20+15)&~15=32
 *   => sizeof=32, alignof=16
 *
 * Layout analysis for S_default { int a; int b; }:
 *   - Same field layout as S_aligned16, no aligned attribute
 *   => sizeof=8, alignof=4
 *
 * All sizeof/alignof/offsetof values are architecture-independent
 * (int=4 bytes on x86-64, AArch64, RISC-V 64, i686).
 *
 * Exercises src/frontend/sema/analysis.rs lines 1941-1945 (struct-level
 * aligned applied after normal field layout) and src/frontend/sema/
 * const_eval.rs (_Alignof returning correct alignment).
 */

#include <stdio.h>

/* Struct 1: THE KEY TEST - struct-level aligned(16) with two int fields */
struct __attribute__((aligned(16))) S_aligned16 {
    int a;
    int b;
};

/* Struct 2: struct-level aligned(16) on a larger struct (crosses 16 boundary) */
struct __attribute__((aligned(16))) S_larger {
    int arr[5];
};

/* Struct 3: default alignment (baseline comparison, same fields as S_aligned16) */
struct S_default {
    int a;
    int b;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof S_aligned16 == 16 (natural 8 rounded up to 16) */
    if (sizeof(struct S_aligned16) == 16)
        printf("sizeof_aligned16: OK\n");
    else {
        printf("sizeof_aligned16: FAIL (got %zu)\n", sizeof(struct S_aligned16));
        failures++;
    }

    /* Test 2: _Alignof(struct S_aligned16) == 16 (THE KEY ALIGNMENT ASSERTION) */
    if (_Alignof(struct S_aligned16) == 16)
        printf("alignof_aligned16: OK\n");
    else {
        printf("alignof_aligned16: FAIL (got %zu)\n", _Alignof(struct S_aligned16));
        failures++;
    }

    /* Test 3: offsetof(struct S_aligned16, a) == 0 (field offsets unchanged) */
    if (offsetof(struct S_aligned16, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %zu)\n", offsetof(struct S_aligned16, a));
        failures++;
    }

    /* Test 4: offsetof(struct S_aligned16, b) == 4 (natural int alignment) */
    if (offsetof(struct S_aligned16, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %zu)\n", offsetof(struct S_aligned16, b));
        failures++;
    }

    /* Test 5: sizeof S_larger == 32 (natural 20 rounds past 16 to 32) */
    if (sizeof(struct S_larger) == 32)
        printf("sizeof_larger: OK\n");
    else {
        printf("sizeof_larger: FAIL (got %zu)\n", sizeof(struct S_larger));
        failures++;
    }

    /* Test 6: sizeof S_default == 8 (baseline, no aligned attribute) */
    if (sizeof(struct S_default) == 8)
        printf("sizeof_default: OK\n");
    else {
        printf("sizeof_default: FAIL (got %zu)\n", sizeof(struct S_default));
        failures++;
    }

    /* Test 7: value correctness through aligned struct */
    {
        struct S_aligned16 s;
        s.a = 12345678;
        s.b = 87654321;
        if (s.a == 12345678 && s.b == 87654321)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    /* Test 8: _Alignof(struct S_larger) == 16 */
    if (_Alignof(struct S_larger) == 16)
        printf("alignof_larger: OK\n");
    else {
        printf("alignof_larger: FAIL (got %zu)\n", _Alignof(struct S_larger));
        failures++;
    }

    if (failures == 0)
        printf("All aligned(16) tests passed\n");
    return failures;
}
