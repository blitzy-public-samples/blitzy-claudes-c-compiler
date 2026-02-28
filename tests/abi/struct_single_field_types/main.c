/*
 * ABI Integration Test: Single-Field Struct Layout and Return Value Classification
 *
 * Verifies that CCC correctly implements sizeof(struct) == sizeof(member) for
 * single-field structs of all basic C types, and that values are correctly
 * returned from noinline functions through the ABI calling convention.
 *
 * Single-field structs are critical for ABI classification because they should
 * be treated equivalently to the underlying scalar type for register allocation.
 * Per the call_abi module, small structs (<=16 bytes) are classified as
 * StructByValReg for register-based return. A single-field struct containing
 * a scalar type should have identical size and alignment to the scalar itself.
 *
 * Tests 10 types x 2 checks = 20 individual verifications:
 *   Part A: sizeof(struct) == sizeof(member) for all 10 types
 *   Part B: Return value survives noinline function call for all 10 types
 *
 * Architecture coverage:
 *   x86-64:  rax / xmm0 return, 8-byte long/ptr
 *   AArch64: x0 / d0 return, 8-byte long/ptr
 *   RISC-V:  a0 / fa0 return, 8-byte long/ptr
 *   i686:    eax / eax:edx return, 4-byte long/ptr
 */

#include <stdio.h>

/* ── Struct Definitions (10 types) ─────────────────────────────────────────── */

struct S_char       { char x; };            /* sizeof == 1 */
struct S_uchar      { unsigned char x; };   /* sizeof == 1 */
struct S_short      { short x; };           /* sizeof == 2 */
struct S_int        { int x; };             /* sizeof == 4 */
struct S_uint       { unsigned int x; };    /* sizeof == 4 */
struct S_long       { long x; };            /* sizeof == 4 on i686, 8 on 64-bit */
struct S_longlong   { long long x; };       /* sizeof == 8 */
struct S_float      { float x; };           /* sizeof == 4 */
struct S_double     { double x; };          /* sizeof == 8 */
struct S_ptr        { void *x; };           /* sizeof == 4 on i686, 8 on 64-bit */

/* ── Return Value Test Functions (all noinline) ────────────────────────────── */

__attribute__((noinline))
struct S_char make_s_char(char v) {
    struct S_char s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_uchar make_s_uchar(unsigned char v) {
    struct S_uchar s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_short make_s_short(short v) {
    struct S_short s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_int make_s_int(int v) {
    struct S_int s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_uint make_s_uint(unsigned int v) {
    struct S_uint s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_long make_s_long(long v) {
    struct S_long s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_longlong make_s_longlong(long long v) {
    struct S_longlong s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_float make_s_float(float v) {
    struct S_float s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_double make_s_double(double v) {
    struct S_double s;
    s.x = v;
    return s;
}

__attribute__((noinline))
struct S_ptr make_s_ptr(void *v) {
    struct S_ptr s;
    s.x = v;
    return s;
}

/* ── Main: Two-Part Testing ────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;

    /* ── Part A: sizeof verification (10 tests) ─────────────────────────── */

    if (sizeof(struct S_char) == sizeof(char)) {
        printf("sizeof_char: OK\n");
    } else {
        printf("sizeof_char: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_char), sizeof(char));
        failures++;
    }

    if (sizeof(struct S_uchar) == sizeof(unsigned char)) {
        printf("sizeof_uchar: OK\n");
    } else {
        printf("sizeof_uchar: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_uchar), sizeof(unsigned char));
        failures++;
    }

    if (sizeof(struct S_short) == sizeof(short)) {
        printf("sizeof_short: OK\n");
    } else {
        printf("sizeof_short: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_short), sizeof(short));
        failures++;
    }

    if (sizeof(struct S_int) == sizeof(int)) {
        printf("sizeof_int: OK\n");
    } else {
        printf("sizeof_int: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_int), sizeof(int));
        failures++;
    }

    if (sizeof(struct S_uint) == sizeof(unsigned int)) {
        printf("sizeof_uint: OK\n");
    } else {
        printf("sizeof_uint: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_uint), sizeof(unsigned int));
        failures++;
    }

    if (sizeof(struct S_long) == sizeof(long)) {
        printf("sizeof_long: OK\n");
    } else {
        printf("sizeof_long: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_long), sizeof(long));
        failures++;
    }

    if (sizeof(struct S_longlong) == sizeof(long long)) {
        printf("sizeof_longlong: OK\n");
    } else {
        printf("sizeof_longlong: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_longlong), sizeof(long long));
        failures++;
    }

    if (sizeof(struct S_float) == sizeof(float)) {
        printf("sizeof_float: OK\n");
    } else {
        printf("sizeof_float: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_float), sizeof(float));
        failures++;
    }

    if (sizeof(struct S_double) == sizeof(double)) {
        printf("sizeof_double: OK\n");
    } else {
        printf("sizeof_double: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_double), sizeof(double));
        failures++;
    }

    if (sizeof(struct S_ptr) == sizeof(void *)) {
        printf("sizeof_ptr: OK\n");
    } else {
        printf("sizeof_ptr: FAIL (struct=%zu, member=%zu)\n",
               sizeof(struct S_ptr), sizeof(void *));
        failures++;
    }

    /* ── Part B: Return value correctness (10 tests) ────────────────────── */

    {
        struct S_char s = make_s_char('A');
        if (s.x == 'A') {
            printf("return_char: OK\n");
        } else {
            printf("return_char: FAIL\n");
            failures++;
        }
    }

    {
        struct S_uchar s = make_s_uchar(200);
        if (s.x == 200) {
            printf("return_uchar: OK\n");
        } else {
            printf("return_uchar: FAIL\n");
            failures++;
        }
    }

    {
        struct S_short s = make_s_short(12345);
        if (s.x == 12345) {
            printf("return_short: OK\n");
        } else {
            printf("return_short: FAIL\n");
            failures++;
        }
    }

    {
        struct S_int s = make_s_int(42);
        if (s.x == 42) {
            printf("return_int: OK\n");
        } else {
            printf("return_int: FAIL\n");
            failures++;
        }
    }

    {
        struct S_uint s = make_s_uint(3000000000U);
        if (s.x == 3000000000U) {
            printf("return_uint: OK\n");
        } else {
            printf("return_uint: FAIL\n");
            failures++;
        }
    }

    {
        struct S_long s = make_s_long(1000000000L);
        if (s.x == 1000000000L) {
            printf("return_long: OK\n");
        } else {
            printf("return_long: FAIL\n");
            failures++;
        }
    }

    {
        struct S_longlong s = make_s_longlong(9876543210LL);
        if (s.x == 9876543210LL) {
            printf("return_longlong: OK\n");
        } else {
            printf("return_longlong: FAIL\n");
            failures++;
        }
    }

    {
        struct S_float s = make_s_float(3.14f);
        if (s.x == 3.14f) {
            printf("return_float: OK\n");
        } else {
            printf("return_float: FAIL\n");
            failures++;
        }
    }

    {
        struct S_double s = make_s_double(2.71828);
        if (s.x == 2.71828) {
            printf("return_double: OK\n");
        } else {
            printf("return_double: FAIL\n");
            failures++;
        }
    }

    {
        struct S_ptr s = make_s_ptr((void *)(long)0xDEAD);
        if (s.x == (void *)(long)0xDEAD) {
            printf("return_ptr: OK\n");
        } else {
            printf("return_ptr: FAIL\n");
            failures++;
        }
    }

    /* ── Summary ─────────────────────────────────────────────────────────── */

    if (failures == 0) printf("All single-field struct tests passed\n");

    return failures;
}
