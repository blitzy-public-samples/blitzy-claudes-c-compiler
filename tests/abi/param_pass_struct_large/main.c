/*
 * ABI integration test: large struct parameter passing (>16 bytes)
 *
 * Verifies that CCC correctly passes large structs (>16 bytes) as function
 * parameters across all four target architectures:
 *
 *   - x86-64  SysV ABI:  large_struct_by_ref: false
 *     LargeStructStack { size: 32 } -- the 32-byte struct is copied by value
 *     onto the stack (MEMORY class).  The struct does NOT consume any GP
 *     register slots, so subsequent scalar arguments can still go in
 *     registers (rdi, rsi, etc.).
 *
 *   - AArch64 AAPCS64:  large_struct_by_ref: true
 *     LargeStructByRefReg { reg_idx, size: 32 } -- the caller copies the
 *     struct to a temporary location and passes a pointer to the copy in a
 *     GP register (e.g., x0).  The pointer consumes one GP register slot.
 *
 *   - RISC-V  LP64D:    large_struct_by_ref: true
 *     LargeStructByRefReg { reg_idx, size: 32 } -- same as AArch64: caller
 *     copies, passes pointer in a GP register (e.g., a0).
 *
 *   - i686    cdecl:    large_struct_by_ref: false
 *     LargeStructStack { size: 32 } -- struct copied by value onto the
 *     stack.  Since cdecl has regparm=0, ALL arguments go on the stack.
 *
 * Uses struct Big { int data[8]; } (32 bytes = 8 * sizeof(int)), which
 * exceeds the 16-byte threshold on ALL architectures, triggering the
 * architecture-specific large-struct parameter passing mechanisms.
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining calls and bypassing the actual large-struct passing ABI.
 */

#include <stdio.h>

/* 32-byte struct: exceeds 16-byte threshold on all architectures */
struct Big {
    int data[8];
};

/* ---------------------------------------------------------------------------
 * Test function 1: verify_big
 *
 * Individually checks each of the 8 members against expected values
 * (data[0]==10, data[1]==20, ..., data[7]==80).
 * Returns 0 if all match, or the 1-based position of the first mismatch.
 * This is the primary test: if any member is wrong, the large-struct
 * passing mechanism has a bug.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_big(struct Big b) {
    if (b.data[0] != 10) return 1;
    if (b.data[1] != 20) return 2;
    if (b.data[2] != 30) return 3;
    if (b.data[3] != 40) return 4;
    if (b.data[4] != 50) return 5;
    if (b.data[5] != 60) return 6;
    if (b.data[6] != 70) return 7;
    if (b.data[7] != 80) return 8;
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 2: verify_big_with_scalars
 *
 * Tests interaction between a large struct parameter and surrounding scalar
 * parameters.
 *   x86-64:        before in rdi, b on stack (MEMORY, no reg consumed),
 *                  after in rsi
 *   AArch64/RISC-V: before in x0/a0, b pointer in x1/a1, after in x2/a2
 *   i686:          all three on the stack
 *
 * Checks before==42, after==99, and all struct members data[i]==(i+1)*100.
 * Returns 0 on success, negative for scalar mismatch, positive for struct
 * member mismatch.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_big_with_scalars(int before, struct Big b, int after) {
    int i;
    if (before != 42) return -1;
    if (after != 99) return -2;
    for (i = 0; i < 8; i++) {
        if (b.data[i] != (i + 1) * 100) return i + 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 3: verify_two_bigs
 *
 * Tests passing two 32-byte structs simultaneously.
 * Verifies a.data[i]==i+1 and b.data[i]==(i+1)*10.
 * This exercises the stack layout for multiple large structs.
 * Returns 0 on success.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int verify_two_bigs(struct Big a, struct Big b) {
    int i;
    for (i = 0; i < 8; i++) {
        if (a.data[i] != i + 1) return i + 1;
    }
    for (i = 0; i < 8; i++) {
        if (b.data[i] != (i + 1) * 10) return -(i + 1);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test function 4: modify_big
 *
 * Modifies the struct parameter (b.data[i] += 1000) and writes the sum
 * through the pointer.  In C, struct Big b is a local copy -- modifying it
 * must NOT affect the caller's original.
 *   x86-64/i686 (by-value on stack): callee directly modifies the stack copy.
 *   AArch64/RISC-V (by-reference): caller allocates a copy and passes a
 *     pointer to it; the callee can modify through this pointer without
 *     affecting the original.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
void modify_big(struct Big b, int *out_sum) {
    int i;
    int sum = 0;
    for (i = 0; i < 8; i++) {
        b.data[i] += 1000;
    }
    for (i = 0; i < 8; i++) {
        sum += b.data[i];
    }
    *out_sum = sum;
}

/* ---------------------------------------------------------------------------
 * Test function 5: sum_big
 *
 * Returns the sum of all 8 members.  Simple verification that all members
 * arrive intact.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int sum_big(struct Big b) {
    int i;
    int sum = 0;
    for (i = 0; i < 8; i++) {
        sum += b.data[i];
    }
    return sum;
}

/* ---------------------------------------------------------------------------
 * Main test driver
 * --------------------------------------------------------------------------- */
int main(void) {
    int failures = 0;
    struct Big b1;
    struct Big b2;
    int result;
    int callee_sum;

    /* Test 1: Basic large struct parameter passing
     * data = {10, 20, 30, 40, 50, 60, 70, 80}
     * Member-by-member verification catches any byte/word corruption. */
    b1.data[0] = 10;
    b1.data[1] = 20;
    b1.data[2] = 30;
    b1.data[3] = 40;
    b1.data[4] = 50;
    b1.data[5] = 60;
    b1.data[6] = 70;
    b1.data[7] = 80;
    result = verify_big(b1);
    if (result == 0) {
        printf("verify_big: OK\n");
    } else {
        printf("verify_big: FAIL\n");
        failures++;
    }

    /* Test 2: Large struct with scalar arguments
     * data[i] = (i+1)*100 => {100, 200, 300, 400, 500, 600, 700, 800}
     * before=42, after=99
     * Catches register/stack classification errors. */
    {
        struct Big b;
        int i;
        for (i = 0; i < 8; i++) {
            b.data[i] = (i + 1) * 100;
        }
        result = verify_big_with_scalars(42, b, 99);
        if (result == 0) {
            printf("big_with_scalars: OK\n");
        } else {
            printf("big_with_scalars: FAIL\n");
            failures++;
        }
    }

    /* Test 3: Two large struct arguments
     * b1.data[i] = i+1 => {1, 2, 3, 4, 5, 6, 7, 8}
     * b2.data[i] = (i+1)*10 => {10, 20, 30, 40, 50, 60, 70, 80}
     * Catches stack layout issues with multiple large structs. */
    {
        int i;
        for (i = 0; i < 8; i++) {
            b1.data[i] = i + 1;
        }
        for (i = 0; i < 8; i++) {
            b2.data[i] = (i + 1) * 10;
        }
        result = verify_two_bigs(b1, b2);
        if (result == 0) {
            printf("two_bigs: OK\n");
        } else {
            printf("two_bigs: FAIL\n");
            failures++;
        }
    }

    /* Test 4: Copy semantics (modify in callee, verify caller unchanged)
     * b1 = {10, 20, 30, 40, 50, 60, 70, 80}
     * callee_sum = (10+1000)+(20+1000)+...+(80+1000) = 8000 + 360 = 8360
     * After call: b1.data[0] must still be 10, b1.data[7] must still be 80
     * Critical for by-reference passing on AArch64/RISC-V. */
    b1.data[0] = 10;
    b1.data[1] = 20;
    b1.data[2] = 30;
    b1.data[3] = 40;
    b1.data[4] = 50;
    b1.data[5] = 60;
    b1.data[6] = 70;
    b1.data[7] = 80;
    callee_sum = 0;
    modify_big(b1, &callee_sum);
    if (callee_sum == 8360 && b1.data[0] == 10 && b1.data[7] == 80) {
        printf("modify_copy: OK\n");
    } else {
        printf("modify_copy: FAIL\n");
        failures++;
    }

    /* Test 5: Sum of members
     * b1 = {10, 20, 30, 40, 50, 60, 70, 80}
     * sum = 10+20+30+40+50+60+70+80 = 360
     * Catches partial data corruption. */
    result = sum_big(b1);
    if (result == 360) {
        printf("sum_big: OK\n");
    } else {
        printf("sum_big: FAIL\n");
        failures++;
    }

    if (failures == 0) {
        printf("All large struct param tests passed\n");
    }
    return failures;
}
