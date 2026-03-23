/*
 * main.c — CCC-compiled caller for ABI cross-linking test
 *
 * This file is compiled with CCC (the compiler under test) and linked against
 * callee.o (compiled with GCC). It calls functions defined in callee.c and
 * verifies their return values to test that CCC correctly generates calling
 * code compatible with GCC-compiled callees across the ABI boundary.
 *
 * Compilation model:
 *   CCC:  ccc -c main.c -o main.o
 *   GCC:  gcc -std=c11 -Wall -c callee.c -o callee.o
 *   Link: gcc main.o callee.o -o test   (or: ccc main.o callee.o -o test)
 *
 * This tests CCC's caller-side ABI: argument passing, stack setup, and
 * return value retrieval when calling GCC-compiled functions.
 */

#include <stdio.h>

/* =========================================================================
 * Struct Definitions
 *
 * These MUST be byte-for-byte identical to the definitions in callee.c.
 * Any layout difference will cause ABI mismatch failures.
 * ========================================================================= */

/* Small struct fitting in 1-2 GP registers (<=16 bytes) */
struct SmallStruct {
    int x;
    int y;
};

/* Large struct (>16 bytes, passed on stack or by reference depending on arch) */
struct LargeStruct {
    long a;
    long b;
    long c;
    long d;
};

/* Struct return value test */
struct ReturnStruct {
    int val;
    float fval;
};

/* =========================================================================
 * Extern Declarations for GCC-Compiled Callee Functions
 * ========================================================================= */

/* Integer parameter tests */
extern int callee_add_one(int x);
extern long callee_long_add_one(long x);
extern int callee_char_to_int(char c);

/* Float parameter tests */
extern float callee_float_double(float f);
extern double callee_double_double(double d);

/* Multiple argument tests (register pressure) */
extern int callee_sum_six_ints(int a, int b, int c, int d, int e, int f);
extern double callee_sum_four_doubles(double a, double b, double c, double d);
extern int callee_mixed_args(int a, double b, int c, float d);

/* Struct parameter tests */
extern int callee_small_struct_sum(struct SmallStruct s);
extern long callee_large_struct_sum(struct LargeStruct s);

/* Return value tests */
extern int callee_return_int(void);
extern float callee_return_float(void);
extern struct ReturnStruct callee_return_struct(void);

/* =========================================================================
 * Float Comparison Helpers
 *
 * Simple approximate equality checks for floating-point comparisons.
 * Tolerance of 0.001 is sufficient for the exact IEEE 754 representable
 * values used in this test.
 * ========================================================================= */

static int float_eq(float a, float b) {
    float diff = a - b;
    if (diff < 0) diff = -diff;
    return diff < 0.001f;
}

static int double_eq(double a, double b) {
    double diff = a - b;
    if (diff < 0) diff = -diff;
    return diff < 0.001;
}

/* =========================================================================
 * Main — Comprehensive ABI Cross-Boundary Tests
 *
 * Each test calls a GCC-compiled callee function and verifies the result.
 * Output lines MUST match expected.stdout exactly.
 * ========================================================================= */

int main(void) {
    int failures = 0;

    /* Test 1: Single int argument via GP register */
    if (callee_add_one(42) == 43) {
        printf("int_arg: OK\n");
    } else {
        printf("int_arg: FAIL\n");
        failures++;
    }

    /* Test 2: Single long argument via GP register (64-bit on LP64) */
    if (callee_long_add_one(1000000000L) == 1000000001L) {
        printf("long_arg: OK\n");
    } else {
        printf("long_arg: FAIL\n");
        failures++;
    }

    /* Test 3: Char argument (promoted to int in GP register) */
    if (callee_char_to_int('A') == 66) {
        printf("char_arg: OK\n");
    } else {
        printf("char_arg: FAIL\n");
        failures++;
    }

    /* Test 4: Float argument via FP register */
    if (float_eq(callee_float_double(3.14f), 6.28f)) {
        printf("float_arg: OK\n");
    } else {
        printf("float_arg: FAIL\n");
        failures++;
    }

    /* Test 5: Double argument via FP register */
    if (double_eq(callee_double_double(2.718281828), 5.436563656)) {
        printf("double_arg: OK\n");
    } else {
        printf("double_arg: FAIL\n");
        failures++;
    }

    /* Test 6: Six int args — GP register exhaustion on x86-64 */
    if (callee_sum_six_ints(1, 2, 3, 4, 5, 6) == 21) {
        printf("multi_int_args: OK\n");
    } else {
        printf("multi_int_args: FAIL\n");
        failures++;
    }

    /* Test 7: Four double args — FP register usage */
    if (double_eq(callee_sum_four_doubles(1.0, 2.0, 3.0, 4.0), 10.0)) {
        printf("multi_float_args: OK\n");
    } else {
        printf("multi_float_args: FAIL\n");
        failures++;
    }

    /* Test 8: Mixed int and float args — interleaved GP/FP register assignment
     * Callee computes: a + c + (int)(b + d) = 10 + 20 + (int)(6.0) = 36 */
    if (callee_mixed_args(10, 2.5, 20, 3.5f) == 36) {
        printf("mixed_int_float_args: OK\n");
    } else {
        printf("mixed_int_float_args: FAIL\n");
        failures++;
    }

    /* Test 9: Small struct by value — register-based struct passing */
    {
        struct SmallStruct ss;
        ss.x = 10;
        ss.y = 20;
        if (callee_small_struct_sum(ss) == 30) {
            printf("small_struct: OK\n");
        } else {
            printf("small_struct: FAIL\n");
            failures++;
        }
    }

    /* Test 10: Large struct by value — stack-based or by-reference passing */
    {
        struct LargeStruct ls;
        ls.a = 1;
        ls.b = 2;
        ls.c = 3;
        ls.d = 4;
        if (callee_large_struct_sum(ls) == 10) {
            printf("large_struct: OK\n");
        } else {
            printf("large_struct: FAIL\n");
            failures++;
        }
    }

    /* Test 11: Integer return value via GP return register */
    if (callee_return_int() == 42) {
        printf("return_int: OK\n");
    } else {
        printf("return_int: FAIL\n");
        failures++;
    }

    /* Test 12: Float return value via FP return register */
    if (float_eq(callee_return_float(), 3.14f)) {
        printf("return_float: OK\n");
    } else {
        printf("return_float: FAIL\n");
        failures++;
    }

    /* Test 13: Struct return value — architecture-specific return convention */
    {
        struct ReturnStruct rs = callee_return_struct();
        if (rs.val == 99 && float_eq(rs.fval, 1.5f)) {
            printf("return_struct: OK\n");
        } else {
            printf("return_struct: FAIL\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("All tests passed\n");
    }

    return failures;
}
