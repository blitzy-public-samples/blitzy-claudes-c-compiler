/*
 * tests/abi/return_value_integer/main.c
 *
 * ABI integration test for integer return values across all four CCC
 * target architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * Verifies:
 *   1. Correct return register usage per architecture ABI:
 *        - x86-64:  rax (eax/ax/al for narrower types)
 *        - AArch64: x0 (w0 for 32-bit and narrower)
 *        - RISC-V:  a0
 *        - i686:    eax (eax:edx pair for 64-bit long long)
 *
 *   2. Sign/zero extension of narrow return types:
 *        - unsigned char (200) and unsigned short (50000) must be
 *          zero-extended — values deliberately chosen with the high bit
 *          set in their type width to catch sign-extension bugs.
 *        - signed char (-42) and short (-1000) must be sign-extended
 *          to full register width.
 *
 *   3. 64-bit returns on 32-bit (i686):
 *        - long long returned in eax:edx register pair.
 *        - Negative long long must have correctly sign-extended high word
 *          in edx.
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual register-based return
 * ABI mechanism.
 */

#include <stdio.h>

/* --- Test functions --- */

/* Returns char 'Z' (90).  Tests basic char return in low byte of GP
   return register. */
__attribute__((noinline))
char return_char(void) {
    return 'Z';
}

/* Returns unsigned char 200 (0xC8).  If the callee sign-extends instead
   of zero-extending, the caller would see 0xFFFFFFC8 (-56). */
__attribute__((noinline))
unsigned char return_uchar(void) {
    return 200;
}

/* Returns signed char -42 (0xD6 in 8-bit).  Must be sign-extended to
   0xFFFFFFD6 in a 32-bit register, not zero-extended to 0x000000D6. */
__attribute__((noinline))
signed char return_schar_neg(void) {
    return -42;
}

/* Returns short 12345.  Tests 16-bit positive short return. */
__attribute__((noinline))
short return_short(void) {
    return 12345;
}

/* Returns short -1000 (0xFC18 in 16-bit).  Must be sign-extended to
   0xFFFFFC18 in a 32-bit register. */
__attribute__((noinline))
short return_short_neg(void) {
    return -1000;
}

/* Returns unsigned short 50000 (0xC350).  If sign-extended instead of
   zero-extended the caller would read a negative value. */
__attribute__((noinline))
unsigned short return_ushort(void) {
    return 50000;
}

/* Returns int 123456789.  Tests standard 32-bit integer return. */
__attribute__((noinline))
int return_int(void) {
    return 123456789;
}

/* Returns int -987654321.  Tests negative 32-bit integer return. */
__attribute__((noinline))
int return_int_neg(void) {
    return -987654321;
}

/* Returns long 1000000000L.  On LP64 (x86-64, AArch64, RISC-V) this is
   64-bit; on ILP32 (i686) it is 32-bit.  The value fits in 32 bits so
   it is portable across both models. */
__attribute__((noinline))
long return_long(void) {
    return 1000000000L;
}

/* Returns long long 123456789012345LL (0x70_4886_0DF7_9B).  Requires
   more than 32 bits.  On 64-bit platforms: single 64-bit register.
   On i686: eax:edx register pair. */
__attribute__((noinline))
long long return_llong(void) {
    return 123456789012345LL;
}

/* Returns long long -999999999999LL.  Tests negative 64-bit return.
   On i686 the high word in edx must be sign-extended. */
__attribute__((noinline))
long long return_llong_neg(void) {
    return -999999999999LL;
}

/* --- Main test driver --- */

int main(void) {
    int failures = 0;

    /* Test 1: char return */
    if (return_char() == 'Z') {
        printf("return_char: OK\n");
    } else {
        printf("return_char: FAIL\n");
        failures++;
    }

    /* Test 2: unsigned char return (zero extension) */
    if (return_uchar() == 200) {
        printf("return_uchar: OK\n");
    } else {
        printf("return_uchar: FAIL\n");
        failures++;
    }

    /* Test 3: signed char negative return (sign extension) */
    if (return_schar_neg() == -42) {
        printf("return_schar_neg: OK\n");
    } else {
        printf("return_schar_neg: FAIL\n");
        failures++;
    }

    /* Test 4: short return */
    if (return_short() == 12345) {
        printf("return_short: OK\n");
    } else {
        printf("return_short: FAIL\n");
        failures++;
    }

    /* Test 5: negative short return (sign extension) */
    if (return_short_neg() == -1000) {
        printf("return_short_neg: OK\n");
    } else {
        printf("return_short_neg: FAIL\n");
        failures++;
    }

    /* Test 6: unsigned short return (zero extension) */
    if (return_ushort() == 50000) {
        printf("return_ushort: OK\n");
    } else {
        printf("return_ushort: FAIL\n");
        failures++;
    }

    /* Test 7: int return */
    if (return_int() == 123456789) {
        printf("return_int: OK\n");
    } else {
        printf("return_int: FAIL\n");
        failures++;
    }

    /* Test 8: negative int return */
    if (return_int_neg() == -987654321) {
        printf("return_int_neg: OK\n");
    } else {
        printf("return_int_neg: FAIL\n");
        failures++;
    }

    /* Test 9: long return (64-bit on LP64, 32-bit on ILP32) */
    if (return_long() == 1000000000L) {
        printf("return_long: OK\n");
    } else {
        printf("return_long: FAIL\n");
        failures++;
    }

    /* Test 10: long long return (eax:edx pair on i686, single 64-bit reg on others) */
    if (return_llong() == 123456789012345LL) {
        printf("return_llong: OK\n");
    } else {
        printf("return_llong: FAIL\n");
        failures++;
    }

    /* Test 11: negative long long return */
    if (return_llong_neg() == -999999999999LL) {
        printf("return_llong_neg: OK\n");
    } else {
        printf("return_llong_neg: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All integer return tests passed\n");
    }

    return failures;
}
