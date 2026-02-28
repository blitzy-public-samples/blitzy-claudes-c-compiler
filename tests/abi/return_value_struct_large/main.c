/*
 * tests/abi/return_value_struct_large/main.c
 *
 * ABI integration test for large struct return via sret (structure return
 * through hidden pointer).
 *
 * struct Big { int data[8]; } is exactly 32 bytes (8 x 4), which exceeds
 * the 16-byte threshold for register-based struct return on ALL target
 * architectures. The caller must therefore allocate space and pass a hidden
 * pointer (sret) as the first argument, through which the callee writes the
 * return value.
 *
 * Architecture-specific sret mechanisms:
 *   - x86-64 SysV ABI:  Caller passes sret pointer in %rdi (first GP arg).
 *                        Callee writes through (%rdi), returns pointer in %rax.
 *                        Normal args shift: base goes to %esi, etc.
 *   - AArch64 AAPCS64:  Caller passes sret pointer in x8 (dedicated register,
 *                        does NOT consume a regular argument slot). base in w0.
 *   - RISC-V LP64D:     Caller passes sret pointer in a0 (first GP arg).
 *                        Callee writes through a0, returns pointer in a0.
 *                        Normal args shift: base goes to a1.
 *   - i686 cdecl:       Caller passes sret pointer on the stack (first arg
 *                        position). Callee writes through pointer, returns
 *                        it in eax.
 *
 * Four test scenarios:
 *   1. Basic large struct return via sret (make_big)
 *   2. Large struct return with a normal parameter (make_big_from)
 *   3. sret return -> large struct by-value argument roundtrip (sum_big)
 *   4. Consecutive sret calls into independent locals (chain_return)
 *
 * All test functions use __attribute__((noinline)) to prevent the compiler
 * from inlining the call and bypassing the sret mechanism.
 */

#include <stdio.h>

struct Big {
    int data[8];
};

__attribute__((noinline))
struct Big make_big(void) {
    struct Big b;
    b.data[0] = 10;
    b.data[1] = 20;
    b.data[2] = 30;
    b.data[3] = 40;
    b.data[4] = 50;
    b.data[5] = 60;
    b.data[6] = 70;
    b.data[7] = 80;
    return b;
}

__attribute__((noinline))
struct Big make_big_from(int base) {
    struct Big b;
    int i;
    for (i = 0; i < 8; i++) {
        b.data[i] = base + i;
    }
    return b;
}

__attribute__((noinline))
int sum_big(struct Big b) {
    int total = 0;
    int i;
    for (i = 0; i < 8; i++) {
        total += b.data[i];
    }
    return total;
}

int main(void) {
    int failures = 0;
    struct Big b;
    int ok;
    int i;

    /* Test 1: Basic large struct return via sret */
    b = make_big();
    ok = 1;
    if (b.data[0] != 10) ok = 0;
    if (b.data[1] != 20) ok = 0;
    if (b.data[2] != 30) ok = 0;
    if (b.data[3] != 40) ok = 0;
    if (b.data[4] != 50) ok = 0;
    if (b.data[5] != 60) ok = 0;
    if (b.data[6] != 70) ok = 0;
    if (b.data[7] != 80) ok = 0;
    if (ok) {
        printf("make_big: OK\n");
    } else {
        printf("make_big: FAIL\n");
        failures++;
    }

    /* Test 2: Large struct return with parameter (sret + normal arg coexistence) */
    b = make_big_from(100);
    ok = 1;
    for (i = 0; i < 8; i++) {
        if (b.data[i] != 100 + i) ok = 0;
    }
    if (ok) {
        printf("make_big_from: OK\n");
    } else {
        printf("make_big_from: FAIL\n");
        failures++;
    }

    /* Test 3: sret return -> large struct by-value argument roundtrip */
    b = make_big();
    if (sum_big(b) == 360) {
        printf("sret_roundtrip: OK\n");
    } else {
        printf("sret_roundtrip: FAIL\n");
        failures++;
    }

    /* Test 4: Consecutive sret calls into different locals */
    {
        struct Big b1, b2;
        b1 = make_big_from(0);
        b2 = make_big_from(10);
        ok = 1;
        for (i = 0; i < 8; i++) {
            if (b1.data[i] != i) ok = 0;
            if (b2.data[i] != 10 + i) ok = 0;
        }
        if (ok) {
            printf("chain_return: OK\n");
        } else {
            printf("chain_return: FAIL\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("All large struct return tests passed\n");
    }

    return failures;
}
