/*
 * ABI Integration Test: Small Struct Return in Registers
 *
 * Verifies that the CCC compiler correctly returns small structs (<=16 bytes)
 * in registers rather than via sret (hidden pointer). Tests struct sizes of
 * 1, 4, 8, and 16 bytes to cover all register-return scenarios across all
 * four target architectures:
 *
 *   x86-64 SysV ABI:
 *     Structs <=8 bytes returned in rax.
 *     Structs 9-16 bytes returned in rax (low) and rdx (high).
 *
 *   AArch64 AAPCS64:
 *     Structs <=16 bytes returned in x0 (low) and x1 (high, if needed).
 *
 *   RISC-V LP64D:
 *     Structs <=16 bytes returned in a0 (low) and a1 (high, if needed).
 *
 *   i686 cdecl:
 *     Structs <=8 bytes returned in eax (low) and edx (high).
 *     Structs >8 bytes use sret on i686, so the 16-byte test exercises sret
 *     on i686 while being register-return on 64-bit architectures.
 */

#include <stdio.h>

/* 1-byte struct: returned in low byte of rax (x86-64), w0 (AArch64),
   a0 (RISC-V), eax (i686) */
struct S1 { char a; };

/* 4-byte struct: returned in low 4 bytes of rax (x86-64), w0 (AArch64),
   a0 (RISC-V), eax (i686) */
struct S4 { int a; };

/* 8-byte struct: returned in rax (x86-64), x0 (AArch64), a0 (RISC-V),
   eax:edx (i686) */
struct S8 { int a; int b; };

/* 16-byte struct: returned in rax:rdx (x86-64), x0:x1 (AArch64),
   a0:a1 (RISC-V). On i686, 16 bytes exceeds the 8-byte register-return
   threshold, so this falls back to sret. The C source is identical --
   the compiler handles the ABI difference transparently. */
struct S16 { long long a; long long b; };

__attribute__((noinline))
struct S1 make_s1(void) {
    struct S1 s;
    s.a = 42;
    return s;
}

__attribute__((noinline))
struct S4 make_s4(void) {
    struct S4 s;
    s.a = 123456;
    return s;
}

__attribute__((noinline))
struct S8 make_s8(void) {
    struct S8 s;
    s.a = 11;
    s.b = 22;
    return s;
}

__attribute__((noinline))
struct S16 make_s16(void) {
    struct S16 s;
    s.a = 100;
    s.b = 200;
    return s;
}

__attribute__((noinline))
struct S8 make_s8_from(int x, int y) {
    struct S8 s;
    s.a = x;
    s.b = y;
    return s;
}

int main(void) {
    int failures = 0;

    /* Test 1: 1-byte struct return in register */
    {
        struct S1 s = make_s1();
        if (s.a == 42) {
            printf("s1_return: OK\n");
        } else {
            printf("s1_return: FAIL\n");
            failures++;
        }
    }

    /* Test 2: 4-byte struct return in register */
    {
        struct S4 s = make_s4();
        if (s.a == 123456) {
            printf("s4_return: OK\n");
        } else {
            printf("s4_return: FAIL\n");
            failures++;
        }
    }

    /* Test 3: 8-byte struct return in register */
    {
        struct S8 s = make_s8();
        if (s.a == 11 && s.b == 22) {
            printf("s8_return: OK\n");
        } else {
            printf("s8_return: FAIL\n");
            failures++;
        }
    }

    /* Test 4: 16-byte struct return in register pair */
    {
        struct S16 s = make_s16();
        if (s.a == 100 && s.b == 200) {
            printf("s16_return: OK\n");
        } else {
            printf("s16_return: FAIL\n");
            failures++;
        }
    }

    /* Test 5: 8-byte struct return with parameters (parameter/return coexistence) */
    {
        struct S8 s = make_s8_from(55, 66);
        if (s.a == 55 && s.b == 66) {
            printf("s8_from_return: OK\n");
        } else {
            printf("s8_from_return: FAIL\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("All small struct return tests passed\n");
    }

    return failures;
}
