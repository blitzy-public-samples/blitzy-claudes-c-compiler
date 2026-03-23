/*
 * ABI integration test: __attribute__((aligned(32))) at the struct level
 *
 * Verifies that struct-level aligned(32) correctly:
 *   1. Raises the struct's overall alignment to 32
 *   2. Rounds the struct size up to the nearest 32-byte boundary
 *   3. Does NOT change individual field offsets (normal layout rules apply)
 *   4. Allows correct value store/load through the aligned struct
 *
 * Layout analysis for S_aligned32 { char a; int b; short c; char d; }:
 *   - Field a (char):  align=1, offset=0,  size=1  -> cursor=1
 *   - Field b (int):   align=4, offset=4,  size=4  -> cursor=8
 *   - Field c (short): align=2, offset=8,  size=2  -> cursor=10
 *   - Field d (char):  align=1, offset=10, size=1  -> cursor=11
 *   - Natural finalize: size=align_up(11,4)=12, align=4
 *   - Struct-level aligned(32): align=32, size=(12+31)&~31=32
 *   => sizeof=32, alignof=32, offsets: a=0, b=4, c=8, d=10
 *
 * Layout analysis for S_larger { int arr[9]; }:
 *   - Field arr (int[9]): align=4, offset=0, size=36 -> cursor=36
 *   - Natural finalize: size=36, align=4
 *   - Struct-level aligned(32): align=32, size=(36+31)&~31=64
 *   => sizeof=64, alignof=32
 *
 * Layout analysis for S_default { char a; int b; short c; char d; }:
 *   - Same field layout as S_aligned32, no aligned attribute
 *   => sizeof=12, alignof=4
 *
 * All sizeof/alignof/offsetof values are architecture-independent
 * (char=1, short=2, int=4 on x86-64, AArch64, RISC-V 64, i686).
 *
 * Exercises src/frontend/sema/analysis.rs lines 1941-1945 (struct-level
 * aligned applied after normal field layout) and src/frontend/sema/
 * const_eval.rs lines 956-960 (_Alignof returning correct alignment).
 */

#include <stdio.h>
#include <stddef.h>

/* Struct 1: struct-level aligned(32) with various field types */
struct __attribute__((aligned(32))) S_aligned32 {
    char a;
    int b;
    short c;
    char d;
};

/* Struct 2: struct-level aligned(32) on a larger struct (crosses 32 boundary) */
struct __attribute__((aligned(32))) S_larger {
    int arr[9];
};

/* Struct 3: default alignment (baseline comparison, same fields as S_aligned32) */
struct S_default {
    char a;
    int b;
    short c;
    char d;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof S_aligned32 == 32 (natural 12 rounded up to 32) */
    if (sizeof(struct S_aligned32) == 32)
        printf("sizeof_aligned32: OK\n");
    else {
        printf("sizeof_aligned32: FAIL (got %zu)\n", sizeof(struct S_aligned32));
        failures++;
    }

    /* Test 2: _Alignof(struct S_aligned32) == 32 (THE KEY ALIGNMENT ASSERTION) */
    if (_Alignof(struct S_aligned32) == 32)
        printf("alignof_aligned32: OK\n");
    else {
        printf("alignof_aligned32: FAIL (got %zu)\n", _Alignof(struct S_aligned32));
        failures++;
    }

    /* Test 3: offsetof(struct S_aligned32, a) == 0 (field offsets unchanged) */
    if (offsetof(struct S_aligned32, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %zu)\n", offsetof(struct S_aligned32, a));
        failures++;
    }

    /* Test 4: offsetof(struct S_aligned32, b) == 4 (int natural alignment) */
    if (offsetof(struct S_aligned32, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %zu)\n", offsetof(struct S_aligned32, b));
        failures++;
    }

    /* Test 5: offsetof(struct S_aligned32, c) == 8 (short after int) */
    if (offsetof(struct S_aligned32, c) == 8)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL (got %zu)\n", offsetof(struct S_aligned32, c));
        failures++;
    }

    /* Test 6: offsetof(struct S_aligned32, d) == 10 (char after short) */
    if (offsetof(struct S_aligned32, d) == 10)
        printf("offsetof_d: OK\n");
    else {
        printf("offsetof_d: FAIL (got %zu)\n", offsetof(struct S_aligned32, d));
        failures++;
    }

    /* Test 7: sizeof S_larger == 64 (natural 36 rounds past 32 to 64) */
    if (sizeof(struct S_larger) == 64)
        printf("sizeof_larger: OK\n");
    else {
        printf("sizeof_larger: FAIL (got %zu)\n", sizeof(struct S_larger));
        failures++;
    }

    /* Test 8: sizeof S_default == 12 (baseline, no aligned attribute) */
    if (sizeof(struct S_default) == 12)
        printf("sizeof_default: OK\n");
    else {
        printf("sizeof_default: FAIL (got %zu)\n", sizeof(struct S_default));
        failures++;
    }

    /* Test 9: value correctness through aligned struct */
    {
        struct S_aligned32 s;
        s.a = 'A';
        s.b = 87654321;
        s.c = 1234;
        s.d = 'Z';
        if (s.a == 'A' && s.b == 87654321 && s.c == 1234 && s.d == 'Z')
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All aligned(32) tests passed\n");
    return failures;
}
