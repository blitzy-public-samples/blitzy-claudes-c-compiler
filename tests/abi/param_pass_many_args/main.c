/*
 * ABI integration test: many-argument function calls
 *
 * Verifies that CCC correctly handles function calls where the number of
 * integer arguments exceeds the available general-purpose argument registers,
 * forcing overflow to the stack.  Tests register-to-stack transition across
 * all four target architectures:
 *
 *   - x86-64  SysV ABI:  max_int_regs = 6  (rdi, rsi, rdx, rcx, r8, r9)
 *   - AArch64 AAPCS64:   max_int_regs = 8  (x0-x7)
 *   - RISC-V  LP64D:     max_int_regs = 8  (a0-a7)
 *   - i686    cdecl:     max_int_regs = 0  (all arguments on stack)
 *
 * With 12 arguments:
 *   x86-64:        args 1-6 in registers, args 7-12 on stack
 *   AArch64/RISC-V: args 1-8 in registers, args 9-12 on stack
 *   i686:          all 12 arguments on stack
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual register/stack-based
 * argument passing ABI mechanism.
 */

#include <stdio.h>

/* --- Test functions --- */

/* Returns the sum of all 12 integer arguments.  Verifies that the aggregate
   result is correct when arguments span both registers and the stack. */
__attribute__((noinline))
int sum_twelve(int a, int b, int c, int d, int e, int f,
               int g, int h, int i, int j, int k, int l) {
    return a + b + c + d + e + f + g + h + i + j + k + l;
}

/* Individually checks each argument against its expected value (a==1, b==2,
   ..., l==12).  Returns 0 if all match, or the position number (1-12) of
   the first mismatch.  This catches positional errors where arguments
   arrive at the wrong parameter slot due to incorrect register/stack offset
   calculation. */
__attribute__((noinline))
int verify_args(int a, int b, int c, int d, int e, int f,
                int g, int h, int i, int j, int k, int l) {
    if (a != 1)  return 1;
    if (b != 2)  return 2;
    if (c != 3)  return 3;
    if (d != 4)  return 4;
    if (e != 5)  return 5;
    if (f != 6)  return 6;
    if (g != 7)  return 7;
    if (h != 8)  return 8;
    if (i != 9)  return 9;
    if (j != 10) return 10;
    if (k != 11) return 11;
    if (l != 12) return 12;
    return 0;
}

/* Returns the sum of 12 long arguments.  On LP64 targets (x86-64, AArch64,
   RISC-V) these are 64-bit; on i686 they are 32-bit.  Tests that wider
   values are correctly passed in both register and stack positions. */
__attribute__((noinline))
long sum_twelve_long(long a, long b, long c, long d, long e, long f,
                     long g, long h, long i, long j, long k, long l) {
    return a + b + c + d + e + f + g + h + i + j + k + l;
}

/* Returns the sum of 10 int arguments.  Called with alternating positive
   and negative values to test mixed-sign arguments across the register/stack
   boundary. */
__attribute__((noinline))
int sum_ten_mixed(int a1, int a2, int a3, int a4, int a5,
                  int a6, int a7, int a8, int a9, int a10) {
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10;
}

/* --- Main test driver --- */

int main(void) {
    int failures = 0;

    /* Test 1: sum_twelve with sequential values 1..12
       Sum = 1+2+3+4+5+6+7+8+9+10+11+12 = 78
       x86-64: args 1-6 in regs, 7-12 on stack
       AArch64/RISC-V: args 1-8 in regs, 9-12 on stack
       i686: all 12 on stack */
    if (sum_twelve(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == 78) {
        printf("sum_twelve: OK\n");
    } else {
        printf("sum_twelve: FAIL\n");
        failures++;
    }

    /* Test 2: verify_args — positional argument verification
       Checks that each argument arrives at the correct parameter slot.
       Returns 0 on success, or the position of the first mismatch.
       Store result in local variable to avoid double-calling on failure. */
    {
        int result = verify_args(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
        if (result == 0) {
            printf("verify_args: OK\n");
        } else {
            printf("verify_args: FAIL\n");
            failures++;
        }
    }

    /* Test 3: sum_twelve_long with long arguments
       Sum = 100+200+300+400+500+600+700+800+900+1000+1100+1200 = 7800
       Tests 64-bit argument passing on LP64; 32-bit on ILP32 */
    if (sum_twelve_long(100L, 200L, 300L, 400L, 500L, 600L,
                        700L, 800L, 900L, 1000L, 1100L, 1200L) == 7800L) {
        printf("sum_twelve_long: OK\n");
    } else {
        printf("sum_twelve_long: FAIL\n");
        failures++;
    }

    /* Test 4: sum_twelve with all negative arguments
       Sum = -(1+2+3+4+5+6+7+8+9+10+11+12) = -78
       Tests sign extension correctness in both register and stack-spilled
       arguments */
    if (sum_twelve(-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12) == -78) {
        printf("sum_negative: OK\n");
    } else {
        printf("sum_negative: FAIL\n");
        failures++;
    }

    /* Test 5: sum_ten_mixed with alternating positive/negative
       Positive: 100+200+300+400+500 = 1500
       Negative: -50-75-100-125-150 = -500
       Total = 1500 - 500 = 1000 */
    if (sum_ten_mixed(100, -50, 200, -75, 300, -100, 400, -125, 500, -150) == 1000) {
        printf("sum_mixed: OK\n");
    } else {
        printf("sum_mixed: FAIL\n");
        failures++;
    }

    /* Test 6: sum_twelve with large values
       Sum = 0x10000*(1+2+3+...+12) = 0x10000*78 = 0x4E0000 = 5111808
       Tests that large integer values are not truncated when spilled to
       the stack */
    if (sum_twelve(0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x60000,
                   0x70000, 0x80000, 0x90000, 0xA0000, 0xB0000, 0xC0000) == 0x4E0000) {
        printf("sum_large_vals: OK\n");
    } else {
        printf("sum_large_vals: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All many-args tests passed\n");
    }

    return failures;
}
