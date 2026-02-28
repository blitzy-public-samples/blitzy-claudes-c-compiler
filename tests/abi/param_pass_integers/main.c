/*
 * ABI integration test: integer parameter passing via GP registers
 *
 * Verifies that CCC correctly passes integer function arguments
 * (char, short, int, long, long long) via general-purpose registers
 * across all four target architectures:
 *
 *   - x86-64  SysV ABI:  max_int_regs: 6
 *     Integer args passed in rdi, rsi, rdx, rcx, r8, r9
 *     (CallArgClass::IntReg { reg_idx }).
 *     After 6 GP args, subsequent integers spill to stack.
 *
 *   - AArch64 AAPCS64:   max_int_regs: 8
 *     Integer args passed in x0-x7 (w0-w7 for 32-bit).
 *     After 8 GP args, spill to stack.
 *
 *   - RISC-V  LP64D:     max_int_regs: 8
 *     Integer args passed in a0-a7.
 *     After 8 GP args, spill to stack.
 *
 *   - i686    cdecl:     max_int_regs: 0
 *     ALL integer arguments go on the stack (CallArgClass::Stack).
 *     The cdecl convention with regparm=0 passes everything via stack.
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual GP register-based parameter
 * passing ABI mechanism.
 *
 * No architecture-specific code is used — the C source is portable across
 * all 4 CCC targets.  All integer values are well within their type's
 * representable range and do not invoke undefined behavior.
 */

#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Test function 1: verify_char
 *
 * Checks that c == 'A' (65).
 * Returns 0 on success, 1 on failure.
 *
 * Tests single char argument in first GP register:
 *   x86-64:  rdi (GP[0])
 *   AArch64: w0  (GP[0])
 *   RISC-V:  a0  (GP[0])
 *   i686:    stack
 *
 * 'A' (65) is safe for both signed and unsigned char.
 * Char is sign-extended or zero-extended when passed in a register;
 * the value 65 has bit 7 clear so both extensions produce the same result.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_char(char c) {
    if (c == 'A') return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 2: verify_short
 *
 * Checks that s == 12345.
 * Returns 0 on success, 1 on failure.
 *
 * Tests single short argument in first GP register:
 *   x86-64:  rdi (GP[0])
 *   AArch64: w0  (GP[0])
 *   RISC-V:  a0  (GP[0])
 *   i686:    stack
 *
 * 12345 fits in signed 16-bit short (range -32768 to 32767).
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_short(short s) {
    if (s == 12345) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 3: verify_int
 *
 * Checks that i == 42.
 * Returns 0 on success, 1 on failure.
 *
 * Tests single int argument in first GP register.
 * This is the most basic integer parameter passing test.
 *   x86-64:  edi (GP[0])
 *   AArch64: w0  (GP[0])
 *   RISC-V:  a0  (GP[0])
 *   i686:    stack
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_int(int i) {
    if (i == 42) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 4: verify_long
 *
 * Checks that l == 1000000000L.
 * Returns 0 on success, 1 on failure.
 *
 * Tests long argument:
 *   On 64-bit targets (x86-64, AArch64, RISC-V): long is 64-bit,
 *     passed in a 64-bit GP register.
 *   On i686: long is 32-bit, passed on the stack.
 *
 * 1000000000 fits in both 32-bit and 64-bit long, making this test
 * portable across all architectures.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_long(long l) {
    if (l == 1000000000L) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 5: verify_longlong
 *
 * Checks that ll == 9876543210LL.
 * Returns 0 on success, 1 on failure.
 *
 * Tests long long (always 64-bit) argument:
 *   x86-64:  rdi (single 64-bit GP register)
 *   AArch64: x0  (single 64-bit GP register)
 *   RISC-V:  a0  (single 64-bit GP register)
 *   i686:    two 32-bit stack words (low word first)
 *
 * 9876543210 (0x24CB016EA) exceeds 32-bit range (max 4294967295),
 * making this a critical test for i686 double-word stack passing.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_longlong(long long ll) {
    if (ll == 9876543210LL) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 6: verify_six_ints
 *
 * Checks a==10, b==20, c==30, d==40, e==50, f==60.
 * Returns 0 on success, 1 on failure.
 *
 * Tests 6 integer arguments — on x86-64 this EXACTLY fills all 6 GP
 * argument registers:
 *   x86-64:  rdi=10, rsi=20, rdx=30, rcx=40, r8=50, r9=60
 * On AArch64: uses x0-x5 (6 of 8 GP regs).
 * On RISC-V:  uses a0-a5 (6 of 8 GP regs).
 * On i686:    all 6 go on the stack.
 *
 * This is a critical boundary test for x86-64 SysV ABI where
 * max_int_regs == 6.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_six_ints(int a, int b, int c, int d, int e, int f) {
    if (a == 10 && b == 20 && c == 30 && d == 40 && e == 50 && f == 60)
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Test function 7: verify_mixed_types
 *
 * Checks c=='Z', s==1000, i==99999, l==123456789L, ll==1234567890123LL.
 * Returns 0 on success, 1 on failure.
 *
 * Tests 5 arguments of different integer types passing through the GP
 * register calling convention.  Each type has a different width but all
 * are classified as integer/GP arguments.
 *
 *   x86-64:  rdi (char), rsi (short), rdx (int), rcx (long), r8 (long long)
 *   AArch64: x0-x4
 *   RISC-V:  a0-a4
 *   i686:    all on stack with appropriate widths
 *            (long long uses two 32-bit stack slots)
 *
 * 123456789L fits within 32-bit range (safe for i686's 32-bit long).
 * 1234567890123LL (0x11F71FB04CB) requires 64-bit representation,
 * testing i686 double-word stack passing.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_mixed_types(char c, short s, int i, long l, long long ll) {
    if (c == 'Z' && s == 1000 && i == 99999 &&
        l == 123456789L && ll == 1234567890123LL)
        return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * main — run all 7 integer parameter passing tests
 * --------------------------------------------------------------------------- */
int main(void) {
    int failures = 0;

    /* Test 1: Single char parameter */
    if (verify_char('A') == 0) {
        printf("single_char: OK\n");
    } else {
        printf("single_char: FAIL\n");
        failures++;
    }

    /* Test 2: Single short parameter */
    if (verify_short(12345) == 0) {
        printf("single_short: OK\n");
    } else {
        printf("single_short: FAIL\n");
        failures++;
    }

    /* Test 3: Single int parameter */
    if (verify_int(42) == 0) {
        printf("single_int: OK\n");
    } else {
        printf("single_int: FAIL\n");
        failures++;
    }

    /* Test 4: Single long parameter */
    if (verify_long(1000000000L) == 0) {
        printf("single_long: OK\n");
    } else {
        printf("single_long: FAIL\n");
        failures++;
    }

    /* Test 5: Single long long parameter */
    if (verify_longlong(9876543210LL) == 0) {
        printf("single_longlong: OK\n");
    } else {
        printf("single_longlong: FAIL\n");
        failures++;
    }

    /* Test 6: Six integer parameters (fills all x86-64 GP arg regs) */
    if (verify_six_ints(10, 20, 30, 40, 50, 60) == 0) {
        printf("six_integers: OK\n");
    } else {
        printf("six_integers: FAIL\n");
        failures++;
    }

    /* Test 7: Mixed integer types (char, short, int, long, long long) */
    if (verify_mixed_types('Z', 1000, 99999, 123456789L, 1234567890123LL) == 0) {
        printf("mixed_int_types: OK\n");
    } else {
        printf("mixed_int_types: FAIL\n");
        failures++;
    }

    if (failures == 0) printf("All integer param tests passed\n");
    return failures;
}
