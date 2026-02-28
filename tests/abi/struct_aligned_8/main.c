/*
 * ABI integration test: __attribute__((aligned(8))) at the struct level
 *
 * Verifies that struct-level aligned(8) correctly:
 *   1. Raises the struct's overall alignment to 8
 *   2. Rounds the struct size up to the nearest 8-byte boundary
 *   3. Does NOT change individual field offsets (normal layout rules apply)
 *   4. Allows correct value store/load through the aligned struct
 *
 * Layout analysis for S_aligned8 { char a; short b; }:
 *   - Field a (char):  align=1, offset=0, size=1  -> cursor=1, max_align=1
 *   - Field b (short): align=2, offset=align_up(1,2)=2, size=2 -> cursor=4, max_align=2
 *   - Natural finalize: size=align_up(4,2)=4, align=2
 *   - Struct-level aligned(8): 8 > 2, so align=8, size=(4+7)&~7=8
 *   => sizeof=8, alignof=8, offsets: a=0, b=2
 *
 * Layout analysis for S_larger { char arr[9]; }:
 *   - Field arr (char[9]): align=1, offset=0, size=9 -> cursor=9, max_align=1
 *   - Natural finalize: size=align_up(9,1)=9, align=1
 *   - Struct-level aligned(8): 8 > 1, so align=8, size=(9+7)&~7=16
 *   => sizeof=16, alignof=8
 *
 * Layout analysis for S_default { char a; short b; }:
 *   - Same field layout as S_aligned8, no aligned attribute
 *   => sizeof=4, alignof=2
 *
 * All sizeof/alignof/offsetof values are architecture-independent
 * (char=1 byte, short=2 bytes on x86-64, AArch64, RISC-V 64, i686).
 *
 * Exercises src/frontend/sema/analysis.rs lines 1941-1945 (struct-level
 * aligned applied after normal field layout) and src/frontend/sema/
 * const_eval.rs (_Alignof returning correct alignment).
 */

#include <stdio.h>

/* Struct 1: THE KEY TEST - struct-level aligned(8) with char and short fields */
struct __attribute__((aligned(8))) S_aligned8 {
    char a;
    short b;
};

/* Struct 2: struct-level aligned(8) on a struct that crosses the 8-byte boundary */
struct __attribute__((aligned(8))) S_larger {
    char arr[9];
};

/* Struct 3: default alignment (baseline comparison, same fields as S_aligned8) */
struct S_default {
    char a;
    short b;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof S_aligned8 == 8 (natural 4 rounded up to 8) */
    if (sizeof(struct S_aligned8) == 8)
        printf("sizeof_aligned8: OK\n");
    else {
        printf("sizeof_aligned8: FAIL (got %zu)\n", sizeof(struct S_aligned8));
        failures++;
    }

    /* Test 2: _Alignof(struct S_aligned8) == 8 (THE KEY ALIGNMENT ASSERTION) */
    if (_Alignof(struct S_aligned8) == 8)
        printf("alignof_aligned8: OK\n");
    else {
        printf("alignof_aligned8: FAIL (got %zu)\n", _Alignof(struct S_aligned8));
        failures++;
    }

    /* Test 3: offsetof(struct S_aligned8, a) == 0 (field offsets unchanged) */
    if (offsetof(struct S_aligned8, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %zu)\n", offsetof(struct S_aligned8, a));
        failures++;
    }

    /* Test 4: offsetof(struct S_aligned8, b) == 2 (natural short alignment) */
    if (offsetof(struct S_aligned8, b) == 2)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %zu)\n", offsetof(struct S_aligned8, b));
        failures++;
    }

    /* Test 5: sizeof S_larger == 16 (natural 9 rounds past 8 to 16) */
    if (sizeof(struct S_larger) == 16)
        printf("sizeof_larger: OK\n");
    else {
        printf("sizeof_larger: FAIL (got %zu)\n", sizeof(struct S_larger));
        failures++;
    }

    /* Test 6: sizeof S_default == 4 (baseline, no aligned attribute) */
    if (sizeof(struct S_default) == 4)
        printf("sizeof_default: OK\n");
    else {
        printf("sizeof_default: FAIL (got %zu)\n", sizeof(struct S_default));
        failures++;
    }

    /* Test 7: value correctness through aligned struct */
    {
        struct S_aligned8 s;
        s.a = 'X';
        s.b = 1234;
        if (s.a == 'X' && s.b == 1234)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    /* Test 8: _Alignof(struct S_larger) == 8 */
    if (_Alignof(struct S_larger) == 8)
        printf("alignof_larger: OK\n");
    else {
        printf("alignof_larger: FAIL (got %zu)\n", _Alignof(struct S_larger));
        failures++;
    }

    if (failures == 0)
        printf("All aligned(8) tests passed\n");
    return failures;
}
