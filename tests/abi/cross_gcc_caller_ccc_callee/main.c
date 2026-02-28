/*
 * main.c — GCC-compiled caller for ABI cross-linking test
 *
 * This file is compiled with GCC and linked against callee.o (compiled with
 * CCC, the compiler under test). It calls functions defined in callee.c and
 * verifies their return values to test that CCC generates ABI-compatible code
 * when receiving parameters and returning values from a GCC-compiled caller.
 *
 * Compilation model:
 *   GCC:  gcc -std=c11 -Wall -c main.c -o main.o
 *   CCC:  ccc -c callee.c -o callee.o
 *   Link: gcc main.o callee.o -o test
 *
 * This tests CCC's callee-side ABI: parameter receiving, function prologue,
 * return value generation, and stack management when called from GCC code.
 */

#include <stdio.h>

/* =========================================================================
 * Struct Definitions
 *
 * These MUST be byte-for-byte identical to the definitions in callee.c.
 * Any layout difference will cause ABI mismatch failures.
 * ========================================================================= */

/* Small struct fitting in 1-2 GP registers (<=16 bytes on LP64) */
struct SmallStruct {
    int x;
    int y;
};

/* Struct with mixed int/float fields */
struct MixedStruct {
    int i;
    float f;
    double d;
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
 * Extern Declarations for CCC-Compiled Callee Functions
 * ========================================================================= */

/* Integer parameter tests */
extern int callee_int_arg(int x);
extern long callee_long_arg(long x);
extern int callee_char_arg(char c);

/* Float parameter tests */
extern float callee_float_arg(float f);
extern double callee_double_arg(double d);

/* Multiple argument tests (register pressure) */
extern int callee_multi_int_args(int a, int b, int c, int d, int e, int f);
extern double callee_multi_float_args(double a, double b, double c, double d);
extern int callee_mixed_int_float_args(int a, double b, int c, float d);

/* Struct parameter tests */
extern int callee_small_struct_return(struct SmallStruct s);
extern int callee_two_field_struct(struct SmallStruct s);
extern long callee_large_struct_arg(struct LargeStruct s);

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
 * Each test calls a CCC-compiled callee function and verifies the result.
 * Output lines MUST match expected.stdout exactly.
 * ========================================================================= */

int main(void) {
    int failures = 0;

    /* Test 1: Single int argument via GP register */
    if (callee_int_arg(42) == 43) {
        printf("int_arg: OK\n");
    } else {
        printf("int_arg: FAIL\n");
        failures++;
    }

    /* Test 2: Single long argument via GP register (64-bit on LP64) */
    if (callee_long_arg(1000000000L) == 1000000001L) {
        printf("long_arg: OK\n");
    } else {
        printf("long_arg: FAIL\n");
        failures++;
    }

    /* Test 3: Char argument (promoted to int in GP register) */
    if (callee_char_arg('A') == 66) {
        printf("char_arg: OK\n");
    } else {
        printf("char_arg: FAIL\n");
        failures++;
    }

    /* Test 4: Float argument via FP register */
    if (float_eq(callee_float_arg(3.14f), 6.28f)) {
        printf("float_arg: OK\n");
    } else {
        printf("float_arg: FAIL\n");
        failures++;
    }

    /* Test 5: Double argument via FP register */
    if (double_eq(callee_double_arg(2.718281828), 5.436563656)) {
        printf("double_arg: OK\n");
    } else {
        printf("double_arg: FAIL\n");
        failures++;
    }

    /* Test 6: Six int args — GP register exhaustion on x86-64 */
    if (callee_multi_int_args(1, 2, 3, 4, 5, 6) == 21) {
        printf("multi_int_args: OK\n");
    } else {
        printf("multi_int_args: FAIL\n");
        failures++;
    }

    /* Test 7: Four double args — FP register usage */
    if (double_eq(callee_multi_float_args(1.0, 2.0, 3.0, 4.0), 10.0)) {
        printf("multi_float_args: OK\n");
    } else {
        printf("multi_float_args: FAIL\n");
        failures++;
    }

    /* Test 8: Mixed int and float args — interleaved GP/FP register assignment
     * Callee computes: a + c + (int)(b + d) = 10 + 20 + (int)(6.0) = 36 */
    if (callee_mixed_int_float_args(10, 2.5, 20, 3.5f) == 36) {
        printf("mixed_int_float_args: OK\n");
    } else {
        printf("mixed_int_float_args: FAIL\n");
        failures++;
    }

    /* Test 9: Small struct by value — receives SmallStruct, returns sum
     * Expected: {10, 20} → 30 */
    {
        struct SmallStruct ss;
        ss.x = 10;
        ss.y = 20;
        if (callee_small_struct_return(ss) == 30) {
            printf("small_struct_return: OK\n");
        } else {
            printf("small_struct_return: FAIL\n");
            failures++;
        }
    }

    /* Test 10: Two-field struct by value — receives SmallStruct, returns sum
     * Expected: {100, 200} → 300 */
    {
        struct SmallStruct ss2;
        ss2.x = 100;
        ss2.y = 200;
        if (callee_two_field_struct(ss2) == 300) {
            printf("two_field_struct: OK\n");
        } else {
            printf("two_field_struct: FAIL\n");
            failures++;
        }
    }

    /* Test 11: Large struct by value — stack-based or by-reference passing
     * Expected: {1, 2, 3, 4} → 10 */
    {
        struct LargeStruct ls;
        ls.a = 1;
        ls.b = 2;
        ls.c = 3;
        ls.d = 4;
        if (callee_large_struct_arg(ls) == 10) {
            printf("large_struct_arg: OK\n");
        } else {
            printf("large_struct_arg: FAIL\n");
            failures++;
        }
    }

    /* Test 12: Integer return value via GP return register */
    if (callee_return_int() == 42) {
        printf("int_return: OK\n");
    } else {
        printf("int_return: FAIL\n");
        failures++;
    }

    /* Test 13: Float return value via FP return register */
    if (float_eq(callee_return_float(), 3.14f)) {
        printf("float_return: OK\n");
    } else {
        printf("float_return: FAIL\n");
        failures++;
    }

    /* Test 14: Struct return value — architecture-specific return convention */
    {
        struct ReturnStruct rs = callee_return_struct();
        if (rs.val == 99 && float_eq(rs.fval, 1.5f)) {
            printf("struct_return: OK\n");
        } else {
            printf("struct_return: FAIL\n");
            failures++;
        }
    }

    return failures;
}
