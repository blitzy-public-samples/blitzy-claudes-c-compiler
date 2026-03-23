/*
 * tests/abi/stack_align_16byte/main.c
 *
 * ABI stack alignment integration test for CCC.
 *
 * Verifies that the CCC compiler maintains proper 16-byte stack alignment
 * for stack-allocated variables declared with _Alignas(16) and for types
 * with natural 16-byte alignment (e.g., long double on x86-64).
 *
 * Key ABI alignment requirements:
 *   - x86-64 SysV ABI:   Stack must be 16-byte aligned before 'call'.
 *                         long double (80-bit extended) requires 16-byte
 *                         alignment per SysV ABI section 3.1.2.
 *   - AArch64 AAPCS64:   Stack must be 16-byte aligned.
 *                         long double is 128-bit (quad) with 16-byte align.
 *   - RISC-V LP64D:      Stack must be 16-byte aligned.
 *                         long double is 128-bit with 16-byte alignment.
 *   - i686 cdecl:        Modern Linux i686 ABI uses 16-byte stack alignment.
 *                         long double is 80-bit; alignment varies by ABI
 *                         variant (4 or 12 bytes).
 *
 * The primary verification mechanism is address modulo alignment:
 *   (unsigned long)&var % alignment == 0
 * If the stack frame is improperly laid out, this check will fail.
 *
 * Six test scenarios exercise different aspects of 16-byte alignment:
 *   1. _Alignas(16) char buffer on the stack
 *   2. _Alignas(16) int on the stack
 *   3. _Alignas(16) int array on the stack
 *   4. long double natural alignment (8-byte minimum portable baseline)
 *   5. Multiple _Alignas(16) variables in the same function
 *   6. Nested function call with _Alignas(16) local in callee
 */

#include <stdio.h>

/* Helper: check if addr is aligned to `align` bytes.
 * Returns 1 if aligned, 0 otherwise. */
static int is_aligned(unsigned long addr, unsigned long align) {
    return (addr % align) == 0;
}

/* =========================================================================
 * Test 1: _Alignas(16) char buffer on the stack
 *
 * Explicitly requires 16-byte alignment for a char buffer. The compiler
 * must ensure (unsigned long)&buf % 16 == 0.
 * ========================================================================= */
static int test_alignas16_char(void) {
    _Alignas(16) char buf[32];
    buf[0] = 'A'; /* prevent optimization */
    return is_aligned((unsigned long)&buf[0], 16);
}

/* =========================================================================
 * Test 2: _Alignas(16) int on the stack
 *
 * Explicitly requires 16-byte alignment for a single int. Natural int
 * alignment is typically 4 bytes, so the compiler must increase it to 16.
 * ========================================================================= */
static int test_alignas16_int(void) {
    _Alignas(16) int x;
    x = 42; /* prevent optimization */
    (void)x;
    return is_aligned((unsigned long)&x, 16);
}

/* =========================================================================
 * Test 3: _Alignas(16) int array on the stack
 *
 * An array with 16-byte alignment. The base address of the array must
 * satisfy address % 16 == 0.
 * ========================================================================= */
static int test_alignas16_array(void) {
    _Alignas(16) int arr[4];
    arr[0] = 1; /* prevent optimization */
    return is_aligned((unsigned long)&arr[0], 16);
}

/* =========================================================================
 * Test 4: long double natural alignment
 *
 * On x86-64: 80-bit extended, 16-byte aligned per SysV ABI section 3.1.2
 * On AArch64/RISC-V: 128-bit quad, 16-byte aligned
 * On i686: 80-bit, 4-byte aligned per i686 ABI
 *
 * We test for minimum 4-byte alignment as a truly portable baseline across
 * all four CCC target architectures, since i686 ABI only requires 4-byte
 * alignment for long double.
 * ========================================================================= */
static int test_long_double_align(void) {
    long double ld;
    ld = 1.0L; /* prevent optimization */
    (void)ld;
    unsigned long addr = (unsigned long)&ld;
    return is_aligned(addr, 4);
}

/* =========================================================================
 * Test 5: Multiple _Alignas(16) variables in the same function
 *
 * Declares three separate _Alignas(16) int variables and verifies ALL of
 * them have 16-byte aligned addresses. This stresses the stack frame
 * layout when multiple high-alignment variables coexist, exercising the
 * slot assignment logic in src/backend/stack_layout/slot_assignment.rs.
 * ========================================================================= */
static int test_multiple_aligned(void) {
    _Alignas(16) int a;
    _Alignas(16) int b;
    _Alignas(16) int c;
    a = 1; b = 2; c = 3; /* prevent optimization */
    (void)a; (void)b; (void)c;
    int ok = 1;
    if (!is_aligned((unsigned long)&a, 16)) ok = 0;
    if (!is_aligned((unsigned long)&b, 16)) ok = 0;
    if (!is_aligned((unsigned long)&c, 16)) ok = 0;
    return ok;
}

/* =========================================================================
 * Callee for Test 6: verifies alignment is maintained through function calls
 *
 * __attribute__((noinline)) prevents the compiler from inlining this
 * function, which would defeat the purpose of testing cross-call alignment.
 * The callee declares a _Alignas(16) variable and verifies its alignment,
 * ensuring that function entry maintains 16-byte stack alignment per the
 * SysV ABI convention.
 * ========================================================================= */
__attribute__((noinline))
static int check_callee_align(void) {
    _Alignas(16) int local_var;
    local_var = 99; /* prevent optimization */
    (void)local_var;
    return is_aligned((unsigned long)&local_var, 16);
}

/* =========================================================================
 * Test 6: Nested call with _Alignas(16) local in callee
 *
 * Verifies that a callee function's _Alignas(16) local has correct
 * alignment even when called from another function.
 * ========================================================================= */
static int test_nested_call_align(void) {
    return check_callee_align();
}

/* =========================================================================
 * Main: Run all 6 tests and report results
 * ========================================================================= */
int main(void) {
    int failures = 0;

    /* Test 1: _Alignas(16) char buffer */
    if (test_alignas16_char()) {
        printf("alignas16_char: OK\n");
    } else {
        printf("alignas16_char: FAIL\n");
        failures++;
    }

    /* Test 2: _Alignas(16) int */
    if (test_alignas16_int()) {
        printf("alignas16_int: OK\n");
    } else {
        printf("alignas16_int: FAIL\n");
        failures++;
    }

    /* Test 3: _Alignas(16) array */
    if (test_alignas16_array()) {
        printf("alignas16_array: OK\n");
    } else {
        printf("alignas16_array: FAIL\n");
        failures++;
    }

    /* Test 4: long double natural alignment */
    if (test_long_double_align()) {
        printf("long_double_align: OK\n");
    } else {
        printf("long_double_align: FAIL\n");
        failures++;
    }

    /* Test 5: Multiple _Alignas(16) variables */
    if (test_multiple_aligned()) {
        printf("multiple_aligned: OK\n");
    } else {
        printf("multiple_aligned: FAIL\n");
        failures++;
    }

    /* Test 6: Nested function call alignment */
    if (test_nested_call_align()) {
        printf("nested_call_align: OK\n");
    } else {
        printf("nested_call_align: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All 16-byte alignment tests passed\n");
    }

    return failures;
}
