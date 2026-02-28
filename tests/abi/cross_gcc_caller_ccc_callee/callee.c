/*
 * ABI Cross-Linking Test: CCC-Compiled Callee
 *
 * This file is compiled with CCC (the compiler under test) and linked against
 * main.o (compiled with GCC). It defines functions that are called from main.c
 * to verify that CCC generates ABI-compatible code when receiving parameters
 * and returning values.
 *
 * Compilation model:
 *   CCC:  ccc -c callee.c -o callee.o
 *   GCC:  gcc -c main.c -o main.o
 *   Link: gcc main.o callee.o -o test
 *
 * Every function is a pure computation with no I/O and no includes.
 * Struct definitions MUST be byte-for-byte identical to those in main.c.
 */

/* -----------------------------------------------------------------------
 * Struct Definitions
 * These must match main.c exactly for correct cross-boundary ABI behavior.
 * ----------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * Integer Parameter Functions
 * Test GP register argument receiving (rdi/x0/a0 on respective ABIs)
 * ----------------------------------------------------------------------- */

/* Receives one int in GP register, returns int in GP register.
 * Expected: callee_int_arg(42) == 43 */
int callee_int_arg(int x) {
    return x + 1;
}

/* Receives one long in GP register, returns long in GP register.
 * Expected: callee_long_arg(1000000000L) == 1000000001L */
long callee_long_arg(long x) {
    return x + 1;
}

/* Receives one char (promoted to int in GP register), returns int.
 * Expected: callee_char_arg('A') == 66  ('A' is 65, 65+1 = 66) */
int callee_char_arg(char c) {
    return (int)(c + 1);
}

/* -----------------------------------------------------------------------
 * Float Parameter Functions
 * Test FP register argument receiving (xmm0/s0/fa0 on respective ABIs)
 * ----------------------------------------------------------------------- */

/* Receives one float in FP register, returns float in FP register.
 * Expected: callee_float_arg(3.14f) ~= 6.28f */
float callee_float_arg(float f) {
    return f * 2.0f;
}

/* Receives one double in FP register, returns double in FP register.
 * Expected: callee_double_arg(2.718281828) ~= 5.436563656 */
double callee_double_arg(double d) {
    return d * 2.0;
}

/* -----------------------------------------------------------------------
 * Multiple Argument Functions (Register Pressure Tests)
 * ----------------------------------------------------------------------- */

/* Receives 6 int args — tests GP register exhaustion on x86-64.
 * x86-64 SysV ABI has exactly 6 GP arg regs: rdi, rsi, rdx, rcx, r8, r9.
 * AArch64 AAPCS64 has 8 GP regs (x0-x7).
 * RISC-V LP64D has 8 GP regs (a0-a7).
 * i686 cdecl passes all args on the stack.
 * Expected: callee_multi_int_args(1, 2, 3, 4, 5, 6) == 21 */
int callee_multi_int_args(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

/* Receives 4 double args — tests FP register usage.
 * Expected: callee_multi_float_args(1.0, 2.0, 3.0, 4.0) == 10.0 */
double callee_multi_float_args(double a, double b, double c, double d) {
    return a + b + c + d;
}

/* Receives mixed int and float args — tests interleaved GP/FP register
 * assignment. Integer args go to GP regs, float/double args go to FP regs,
 * independently indexed.
 *
 * Computation: a + c + (int)(b + d)
 *   With a=10, b=2.5, c=20, d=3.5f:
 *   b + d = 2.5 + 3.5 = 6.0 (d promoted to double for addition)
 *   (int)(6.0) = 6
 *   10 + 20 + 6 = 36
 * Expected: callee_mixed_int_float_args(10, 2.5, 20, 3.5f) == 36 */
int callee_mixed_int_float_args(int a, double b, int c, float d) {
    return a + c + (int)(b + d);
}

/* -----------------------------------------------------------------------
 * Struct Parameter Functions
 * Test struct passing by value (registers for small, stack/ref for large)
 * ----------------------------------------------------------------------- */

/* Receives a SmallStruct by value in registers (1-2 GP regs).
 * Expected: callee_small_struct_return({10, 20}) == 30 */
int callee_small_struct_return(struct SmallStruct s) {
    return s.x + s.y;
}

/* Receives a SmallStruct by value — second test with different values.
 * Expected: callee_two_field_struct({100, 200}) == 300 */
int callee_two_field_struct(struct SmallStruct s) {
    return s.x + s.y;
}

/* Receives a LargeStruct (>16 bytes).
 * On x86-64: passed on the stack (MEMORY class).
 * On AArch64: passed by reference (pointer in GP register).
 * On RISC-V: passed on the stack or split across registers.
 * On i686: passed on the stack.
 * Expected: callee_large_struct_arg({1, 2, 3, 4}) == 10 */
long callee_large_struct_arg(struct LargeStruct s) {
    return s.a + s.b + s.c + s.d;
}

/* -----------------------------------------------------------------------
 * Return Value Functions
 * Test return value conventions across different types
 * ----------------------------------------------------------------------- */

/* Returns an int in GP return register (eax/w0/a0).
 * Expected: callee_return_int() == 42 */
int callee_return_int(void) {
    return 42;
}

/* Returns a float in FP return register (xmm0/s0/fa0).
 * Expected: callee_return_float() ~= 3.14f */
float callee_return_float(void) {
    return 3.14f;
}

/* Returns a struct via the architecture-specific struct return convention:
 * x86-64: Small struct returned in rax (val in low 32 bits, fval in bits 32-63)
 *         or via rax+xmm0 depending on classification.
 * AArch64: Small struct returned in x0 or x0+s0 depending on HFA classification.
 * RISC-V: Small struct returned in a0+fa0 depending on float struct convention.
 * i686: Struct returned via hidden sret pointer in first arg.
 * Expected: callee_return_struct().val == 99, callee_return_struct().fval ~= 1.5f */
struct ReturnStruct callee_return_struct(void) {
    struct ReturnStruct r;
    r.val = 99;
    r.fval = 1.5f;
    return r;
}
