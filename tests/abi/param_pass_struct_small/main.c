/*
 * ABI integration test: small struct parameter passing (<=16 bytes)
 *
 * Verifies that CCC correctly passes small structs (<=16 bytes) as function
 * parameters by value in registers across all four target architectures:
 *
 *   - x86-64  SysV ABI:  max_int_regs: 6, use_sysv_struct_classification: true
 *     An 8-byte struct { int a; int b; } is classified as a single INTEGER
 *     eightbyte and passed in one GP register (e.g., rdi).  Both int fields
 *     are packed into one 64-bit register.
 *     CallArgClass::StructByValReg { base_reg_idx, size: 8 }
 *
 *   - AArch64 AAPCS64:  max_int_regs: 8, large_struct_by_ref: true (>16 only)
 *     An 8-byte struct fits in one GP register (x0).  Both int fields packed.
 *     CallArgClass::StructByValReg { base_reg_idx, size: 8 }
 *
 *   - RISC-V  LP64D:    max_int_regs: 8
 *     An all-integer 8-byte struct goes into one GP register (a0).
 *     CallArgClass::StructByValReg { base_reg_idx, size: 8 }
 *
 *   - i686    cdecl:    max_int_regs: 0 (regparm=0)
 *     ALL arguments go on the stack.  The 8-byte struct is pushed by value.
 *     CallArgClass::StructByValStack { size: 8 }
 *
 * The primary test struct is struct S { int a; int b; } (8 bytes), which
 * fits in registers on all architectures.  Additional structs of 4, 12,
 * and 16 bytes exercise boundary conditions.
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining calls and bypassing the actual register-based small-struct
 * parameter passing ABI mechanism.
 */

#include <stdio.h>

/* 8-byte struct: the primary test case -- 2 ints packed into one register */
struct S {
    int a;
    int b;
};

/* 4-byte struct: single int member, minimal meaningful struct */
struct S4 {
    int x;
};

/* 12-byte struct: 3 ints, exercises partial second eightbyte on x86-64 */
struct S12 {
    int x;
    int y;
    int z;
};

/* 16-byte struct: boundary case, maximum size for register passing */
struct S16 {
    long long a;
    long long b;
};

/* ---------------------------------------------------------------------------
 * Test function 1: verify_s
 *
 * Checks that s.a == 10 and s.b == 20.
 * Returns 0 on success, 1 on failure.
 * This is the PRIMARY test case: struct S { int a; int b; } passed by value,
 * verifying both members arrive correctly through the ABI.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_s(struct S s) {
    if (s.a != 10) return 1;
    if (s.b != 20) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 2: verify_s4
 *
 * Checks that s.x == 42.
 * Returns 0 on success, 1 on failure.
 * Tests the minimal 4-byte struct case (single int member).
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_s4(struct S4 s) {
    if (s.x != 42) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 3: verify_s12
 *
 * Checks that s.x == 1, s.y == 2, s.z == 3.
 * Returns 0 on success, 1 on failure.
 * Tests a 12-byte struct that spans a partial second eightbyte on x86-64.
 * On x86-64 SysV: classified as two INTEGER eightbytes (8 bytes in one GP
 * register + 4 bytes in a second GP register).
 * On AArch64/RISC-V: occupies two GP registers.
 * On i686: pushed by value on the stack.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_s12(struct S12 s) {
    if (s.x != 1) return 1;
    if (s.y != 2) return 1;
    if (s.z != 3) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 4: verify_s16
 *
 * Checks that s.a == 100 and s.b == 200.
 * Returns 0 on success, 1 on failure.
 * Tests the maximum 16-byte boundary for register passing.
 * On x86-64 SysV: two INTEGER eightbytes in rdi+rsi.
 * On AArch64: x0+x1.
 * On RISC-V: a0+a1.
 * On i686: pushed by value on the stack.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_s16(struct S16 s) {
    if (s.a != 100) return 1;
    if (s.b != 200) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 5: verify_s_with_scalars
 *
 * Checks that before == 55, s.a == 10, s.b == 20, and after == 77.
 * Returns 0 on success, 1 on failure.
 * Tests that a small struct parameter interleaved with scalar arguments
 * does not corrupt surrounding register assignments.
 * On x86-64: before in rdi, struct packed in rsi, after in rdx.
 * On AArch64: before in x0, struct in x1, after in x2.
 * On RISC-V: before in a0, struct in a1, after in a2.
 * On i686: all three on the stack.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_s_with_scalars(int before, struct S s, int after) {
    if (before != 55) return 1;
    if (s.a != 10) return 1;
    if (s.b != 20) return 1;
    if (after != 77) return 1;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 6: sum_s
 *
 * Returns s.a + s.b.  Simple aggregate correctness check that both
 * members arrive intact through the ABI.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int sum_s(struct S s) {
    return s.a + s.b;
}

/* ---------------------------------------------------------------------------
 * Test function 7: modify_s
 *
 * Modifies the struct parameter copy (s.a += 1000, s.b += 2000) and
 * writes the resulting sum (s.a + s.b) through the out_sum pointer.
 * In C, struct S s is a local copy -- modifying it must NOT affect the
 * caller's original.  This tests copy semantics of the ABI.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
void modify_s(struct S s, int *out_sum) {
    s.a += 1000;
    s.b += 2000;
    *out_sum = s.a + s.b;
}

/* ---------------------------------------------------------------------------
 * Main test driver
 * --------------------------------------------------------------------------- */
int main(void) {
    int failures = 0;
    int result;
    int callee_sum;
    struct S s;
    struct S4 s4;
    struct S12 s12;
    struct S16 s16;

    /* Test 1: Primary small struct parameter passing
     * struct S { int a; int b; } with a=10, b=20.
     * Member-by-member verification catches any byte/word corruption
     * in the register-based passing mechanism. */
    s.a = 10;
    s.b = 20;
    result = verify_s(s);
    if (result == 0) {
        printf("verify_s: OK\n");
    } else {
        printf("verify_s: FAIL\n");
        failures++;
    }

    /* Test 2: Minimal 4-byte struct
     * struct S4 { int x; } with x=42.
     * Single-member struct exercises the minimal struct ABI path. */
    s4.x = 42;
    result = verify_s4(s4);
    if (result == 0) {
        printf("verify_s4: OK\n");
    } else {
        printf("verify_s4: FAIL\n");
        failures++;
    }

    /* Test 3: 12-byte struct (partial second eightbyte on x86-64)
     * struct S12 { int x; int y; int z; } with x=1, y=2, z=3.
     * On x86-64 SysV this spans two eightbytes: first 8 bytes (x, y)
     * in one GP register, last 4 bytes (z) in a second GP register. */
    s12.x = 1;
    s12.y = 2;
    s12.z = 3;
    result = verify_s12(s12);
    if (result == 0) {
        printf("verify_s12: OK\n");
    } else {
        printf("verify_s12: FAIL\n");
        failures++;
    }

    /* Test 4: 16-byte boundary struct (maximum for register passing)
     * struct S16 { long long a; long long b; } with a=100, b=200.
     * This is the largest struct that still qualifies for register
     * passing (<=16 bytes) on all architectures. */
    s16.a = 100;
    s16.b = 200;
    result = verify_s16(s16);
    if (result == 0) {
        printf("verify_s16: OK\n");
    } else {
        printf("verify_s16: FAIL\n");
        failures++;
    }

    /* Test 5: Small struct with surrounding scalar arguments
     * before=55, s={10,20}, after=77.
     * Verifies that the struct parameter does not corrupt the
     * register assignments of surrounding scalar arguments. */
    s.a = 10;
    s.b = 20;
    result = verify_s_with_scalars(55, s, 77);
    if (result == 0) {
        printf("s_with_scalars: OK\n");
    } else {
        printf("s_with_scalars: FAIL\n");
        failures++;
    }

    /* Test 6: Sum of struct members
     * s={10,20}, expected sum = 10+20 = 30.
     * Simple aggregate correctness check. */
    s.a = 10;
    s.b = 20;
    result = sum_s(s);
    if (result == 30) {
        printf("sum_s: OK\n");
    } else {
        printf("sum_s: FAIL\n");
        failures++;
    }

    /* Test 7: Copy semantics -- modify in callee, verify caller unchanged
     * s={10,20}, callee does s.a+=1000 s.b+=2000, writes sum.
     * callee_sum should be (10+1000)+(20+2000) = 1010+2020 = 3030.
     * Caller's s.a must still be 10, s.b must still be 20.
     * Critical test: ensures the ABI's copy semantics work correctly. */
    s.a = 10;
    s.b = 20;
    callee_sum = 0;
    modify_s(s, &callee_sum);
    if (callee_sum == 3030 && s.a == 10 && s.b == 20) {
        printf("modify_copy: OK\n");
    } else {
        printf("modify_copy: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All small struct param tests passed\n");
    }
    return failures;
}
