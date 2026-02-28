/*
 * ABI integration test: empty struct parameter and return value handling
 *
 * Verifies that CCC correctly handles empty structs as a GCC extension.
 * In C (GCC behavior), `struct S { }` has sizeof == 0, which differs from
 * C++ where empty structs have sizeof == 1.
 *
 * This test exercises the ZeroSizeSkip classification in the call ABI module
 * (src/backend/call_abi.rs) which ensures zero-size struct arguments consume
 * no register or stack space during function calls.
 *
 * Key ABI behaviors tested:
 *   - sizeof(struct Empty) == 0 (GCC extension)
 *   - sizeof(struct ZeroArray) == 0 (struct with only char x[0])
 *   - Empty struct parameter classified as ZeroSizeSkip:
 *     does NOT consume register or stack space
 *   - Empty struct between two int parameters does NOT shift
 *     the second int's register assignment
 *   - Multiple empty struct parameters are all ZeroSizeSkip
 *   - Returning an empty struct does not crash
 *
 * All test functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining the call and bypassing the actual ABI parameter passing
 * mechanism.
 *
 * Architecture independence: sizeof values (0) are identical on all four
 * CCC targets (x86-64, AArch64, RISC-V 64, i686). No expected.skip.*
 * files are needed.
 */

#include <stdio.h>

/* Empty struct - GCC extension: sizeof == 0 */
struct Empty { };

/* Struct with a zero-length array - also sizeof == 0 */
struct ZeroArray { char x[0]; };

/* Normal struct for reference - sizeof == 4 */
struct Normal { int x; };

/* ---------------------------------------------------------------------------
 * check_after_empty
 *
 * Receives an empty struct followed by an int. The empty struct must be
 * classified as ZeroSizeSkip and consume NO register/stack space. val
 * should arrive in the FIRST argument register (rdi on x86-64, x0 on
 * AArch64, a0 on RISC-V) or first stack slot on i686.
 *
 * Returns 0 on success, 1 on failure.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int check_after_empty(struct Empty e, int val) {
    if (val == 42) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * check_between_ints
 *
 * Receives an int, then an empty struct, then another int. The empty struct
 * between the two ints must NOT shift the second int's register assignment.
 * a should be in first register, b in second register (not third).
 *
 * Returns 0 on success, 1 on failure.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int check_between_ints(int a, struct Empty e, int b) {
    if (a == 10 && b == 20) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * check_multiple_empty
 *
 * Two empty structs before an int. Both must be ZeroSizeSkip. val must
 * still arrive in the first argument register.
 *
 * Returns 0 on success, 1 on failure.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int check_multiple_empty(struct Empty e1, struct Empty e2, int val) {
    if (val == 99) return 0;
    return 1;
}

/* ---------------------------------------------------------------------------
 * return_empty
 *
 * Returns an empty struct. This exercises the return value path for
 * zero-size types. The caller just verifies the call completes without
 * crashing.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
struct Empty return_empty(void) {
    struct Empty e;
    return e;
}

/* ---------------------------------------------------------------------------
 * check_zero_array
 *
 * Same as check_after_empty but using struct ZeroArray { char x[0]; }
 * which also has size 0. This verifies the comment in call_abi.rs:
 * "Zero-size struct argument (e.g., `struct { char x[0]; }`)".
 *
 * Returns 0 on success, 1 on failure.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
int check_zero_array(struct ZeroArray z, int val) {
    if (val == 55) return 0;
    return 1;
}

int main(void) {
    int failures = 0;
    struct Empty e;
    struct ZeroArray z;

    /* Test 1: sizeof(struct Empty) == 0 */
    if (sizeof(struct Empty) == 0)
        printf("sizeof_empty: OK\n");
    else {
        printf("sizeof_empty: FAIL\n");
        failures++;
    }

    /* Test 2: sizeof(struct ZeroArray) == 0 */
    if (sizeof(struct ZeroArray) == 0)
        printf("sizeof_zero_array: OK\n");
    else {
        printf("sizeof_zero_array: FAIL\n");
        failures++;
    }

    /* Test 3: Empty struct before int parameter */
    if (check_after_empty(e, 42) == 0)
        printf("pass_empty_before_int: OK\n");
    else {
        printf("pass_empty_before_int: FAIL\n");
        failures++;
    }

    /* Test 4: Empty struct between two int parameters */
    if (check_between_ints(10, e, 20) == 0)
        printf("pass_empty_between_ints: OK\n");
    else {
        printf("pass_empty_between_ints: FAIL\n");
        failures++;
    }

    /* Test 5: Multiple empty structs before int */
    if (check_multiple_empty(e, e, 99) == 0)
        printf("pass_multiple_empty: OK\n");
    else {
        printf("pass_multiple_empty: FAIL\n");
        failures++;
    }

    /* Test 6: Return empty struct - just verify it doesn't crash */
    return_empty();
    printf("return_empty: OK\n");

    /* Test 7: Zero-length array struct before int */
    if (check_zero_array(z, 55) == 0)
        printf("pass_zero_array: OK\n");
    else {
        printf("pass_zero_array: FAIL\n");
        failures++;
    }

    if (failures == 0)
        printf("All empty struct tests passed\n");

    return failures;
}
