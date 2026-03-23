/*
 * callee.c — GCC-compiled callee for ABI cross-linking test
 *
 * This file is compiled with the system GCC (or cross-GCC for non-native
 * architectures) and defines functions that are called from main.c (compiled
 * with CCC). This tests that CCC's call-site code generation correctly
 * interfaces with GCC-compiled functions across the ABI boundary.
 *
 * Compilation:
 *   Native x86-64:  gcc -std=c11 -Wall -c callee.c -o callee.o
 *   AArch64 cross:  aarch64-linux-gnu-gcc -std=c11 -Wall -c callee.c -o callee.o
 *   RISC-V cross:   riscv64-linux-gnu-gcc -std=c11 -Wall -c callee.c -o callee.o
 *   i686 cross:     i686-linux-gnu-gcc -std=c11 -Wall -c callee.c -o callee.o
 *
 * No #include directives — this file is entirely self-contained.
 * No main() function — main.c provides the entry point.
 * No I/O — functions are pure computational only.
 */

/* =========================================================================
 * Struct Definitions
 *
 * These MUST be byte-for-byte identical to the definitions in main.c.
 * Any layout difference will cause ABI mismatch failures at link time
 * or produce incorrect results at runtime.
 * ========================================================================= */

/* Small struct fitting in 1-2 GP registers (<=16 bytes).
 * On x86-64 SysV ABI: passed in rdi (packed as two 32-bit ints in one 64-bit reg).
 * On AArch64 AAPCS64: passed in x0 (8 bytes, fits in one GP register).
 * On RISC-V LP64D: passed in a0 (8 bytes, fits in one GP register).
 * On i686 cdecl: passed on the stack (two 4-byte pushes). */
struct SmallStruct {
    int x;
    int y;
};

/* Large struct (>16 bytes, passed on stack or by reference depending on arch).
 * On x86-64 SysV ABI: MEMORY class — copied to stack by caller.
 * On AArch64 AAPCS64: >16 bytes — passed by reference (pointer in GP register).
 * On RISC-V LP64D: >2×XLEN — passed by reference (pointer in GP register).
 * On i686 cdecl: pushed entirely on the stack. */
struct LargeStruct {
    long a;
    long b;
    long c;
    long d;
};

/* Struct return value test.
 * Mixed int + float fields exercise different return conventions:
 * On x86-64 SysV ABI: val in eax (INTEGER class), fval in xmm0 (SSE class).
 * On AArch64 AAPCS64: returned in registers (x0 for int, s0 for float, or packed).
 * On RISC-V LP64D: int field in a0, float field in fa0.
 * On i686 cdecl: returned via hidden sret pointer. */
struct ReturnStruct {
    int val;
    float fval;
};

/* =========================================================================
 * Integer Parameter Functions
 *
 * Test basic GP register argument passing and return conventions.
 * ========================================================================= */

/* Add 1 to integer argument. Tests single GP register argument passing.
 * Expected: callee_add_one(42) == 43 */
int callee_add_one(int x) {
    return x + 1;
}

/* Add 1 to long argument. Tests 64-bit GP register passing.
 * On LP64 platforms (x86-64, AArch64, RISC-V), long is 64-bit.
 * On ILP32 (i686), long is 32-bit but the value 1000000000 fits in 32 bits.
 * Expected: callee_long_add_one(1000000000) == 1000000001 */
long callee_long_add_one(long x) {
    return x + 1;
}

/* Convert char to int, add 1. Tests char promotion in GP register.
 * The char parameter is widened to int-width in the GP register per the ABI.
 * Expected: callee_char_to_int('A') == 66  (char 'A' is 65, +1 = 66) */
int callee_char_to_int(char c) {
    return (int)(c + 1);
}

/* =========================================================================
 * Floating-Point Parameter Functions
 *
 * Test FP register argument passing and return conventions.
 * ========================================================================= */

/* Double a float value. Tests FP register argument passing.
 * On x86-64: float arg in xmm0, result returned in xmm0.
 * On AArch64: float arg in s0, result returned in s0.
 * On RISC-V: float arg in fa0, result returned in fa0.
 * On i686: float arg on x87 stack or pushed to stack.
 * Expected: callee_float_double(3.14f) approximately equals 6.28f */
float callee_float_double(float f) {
    return f * 2.0f;
}

/* Double a double value. Tests double FP register passing.
 * On x86-64: double arg in xmm0, result returned in xmm0.
 * On AArch64: double arg in d0, result returned in d0.
 * On RISC-V: double arg in fa0, result returned in fa0.
 * On i686: double arg on x87 stack or pushed to stack.
 * Expected: callee_double_double(2.718281828) approximately equals 5.436563656 */
double callee_double_double(double d) {
    return d * 2.0;
}

/* =========================================================================
 * Multiple Argument Functions (Register Pressure Tests)
 *
 * Test GP/FP register exhaustion and interleaved register assignment.
 * ========================================================================= */

/* Sum 6 int args. Tests GP register exhaustion.
 * On x86-64 SysV ABI: exactly 6 GP arg regs (rdi, rsi, rdx, rcx, r8, r9).
 *   This call fills ALL 6 GP argument registers — a 7th would spill to stack.
 * On AArch64: uses x0-x5 of the 8 available GP argument registers.
 * On RISC-V: uses a0-a5 of the 8 available GP argument registers.
 * On i686: all 6 arguments pushed to the stack (cdecl has no register args).
 * Expected: callee_sum_six_ints(1, 2, 3, 4, 5, 6) == 21 */
int callee_sum_six_ints(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

/* Sum 4 double args. Tests FP register usage with multiple arguments.
 * On x86-64: args in xmm0, xmm1, xmm2, xmm3.
 * On AArch64: args in d0, d1, d2, d3.
 * On RISC-V: args in fa0, fa1, fa2, fa3.
 * On i686: all doubles pushed to stack (8 bytes each).
 * Expected: callee_sum_four_doubles(1.0, 2.0, 3.0, 4.0) == 10.0 */
double callee_sum_four_doubles(double a, double b, double c, double d) {
    return a + b + c + d;
}

/* Mixed int and float args. Tests interleaved GP/FP register assignment.
 * On x86-64 SysV ABI: int args go in GP regs, float/double in XMM regs.
 *   a -> edi (GP reg 0), b -> xmm0 (FP reg 0), c -> esi (GP reg 1), d -> xmm1 (FP reg 1).
 * On AArch64: a -> w0, b -> d0, c -> w1, d -> s1.
 * On RISC-V: a -> a0, b -> fa0, c -> a1, d -> fa1.
 * On i686: all pushed to stack in reverse order.
 *
 * Computation with inputs (10, 2.5, 20, 3.5f):
 *   b + d = 2.5 + 3.5 = 6.0  (float d promoted to double for addition)
 *   (int)(6.0) = 6
 *   a + c + 6 = 10 + 20 + 6 = 36
 * Expected: callee_mixed_args(10, 2.5, 20, 3.5f) == 36 */
int callee_mixed_args(int a, double b, int c, float d) {
    return a + c + (int)(b + d);
}

/* =========================================================================
 * Struct Parameter Functions
 *
 * Test struct passing conventions across the ABI boundary.
 * ========================================================================= */

/* Sum fields of small struct. Tests register-based struct passing.
 * SmallStruct is 8 bytes (2 × int) — fits in a single 64-bit register on LP64.
 * On x86-64: passed in rdi (both fields packed into one 64-bit register).
 * On AArch64: passed in x0 (8 bytes fits in one GP register).
 * On RISC-V: passed in a0 (8 bytes fits in one GP register).
 * On i686: passed on the stack (8 bytes pushed).
 * Expected: callee_small_struct_sum({10, 20}) == 30 */
int callee_small_struct_sum(struct SmallStruct s) {
    return s.x + s.y;
}

/* Sum fields of large struct. Tests stack-based or by-reference struct passing.
 * LargeStruct is 32 bytes on LP64 (4 × long × 8 bytes), 16 bytes on ILP32 (4 × long × 4 bytes).
 * On x86-64: >16 bytes, MEMORY class — entire struct copied to caller's stack.
 * On AArch64: >16 bytes — passed by reference (pointer in GP register).
 * On RISC-V: >2×XLEN — passed by reference (pointer in GP register).
 * On i686: 16 bytes — may be passed on stack directly.
 * Expected: callee_large_struct_sum({1, 2, 3, 4}) == 10 */
long callee_large_struct_sum(struct LargeStruct s) {
    return s.a + s.b + s.c + s.d;
}

/* =========================================================================
 * Return Value Functions
 *
 * Test return value conventions for different types.
 * ========================================================================= */

/* Return an integer. Tests GP return register.
 * On x86-64: returned in eax.
 * On AArch64: returned in w0.
 * On RISC-V: returned in a0.
 * On i686: returned in eax.
 * Expected: callee_return_int() == 42 */
int callee_return_int(void) {
    return 42;
}

/* Return a float. Tests FP return register.
 * On x86-64: returned in xmm0.
 * On AArch64: returned in s0.
 * On RISC-V: returned in fa0.
 * On i686: returned on x87 stack (ST(0)).
 * Expected: callee_return_float() approximately equals 3.14f */
float callee_return_float(void) {
    return 3.14f;
}

/* Return a struct. Tests struct return convention.
 * ReturnStruct has mixed int + float fields (8 bytes total).
 * On x86-64 SysV: val in eax (INTEGER), fval in xmm0 (SSE) — split across register classes.
 * On AArch64: may be returned in registers (x0 for int, s0 for float) or as HFA.
 * On RISC-V LP64D: int field in a0, float field in fa0 (hardware float convention).
 * On i686: returned via hidden sret pointer (caller allocates, passes pointer in first arg).
 * Expected: callee_return_struct().val == 99 && callee_return_struct().fval approximately equals 1.5f */
struct ReturnStruct callee_return_struct(void) {
    struct ReturnStruct r;
    r.val = 99;
    r.fval = 1.5f;
    return r;
}
