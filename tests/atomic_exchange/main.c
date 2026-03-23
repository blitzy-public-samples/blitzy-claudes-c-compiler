/*
 * Integration test: __atomic_exchange_n and __atomic_compare_exchange_n builtins
 *
 * Verifies that CCC correctly lowers GCC-style atomic exchange and
 * compare-exchange (CAS) operations across all 4 target architectures
 * (x86-64, AArch64, RISC-V 64, i686).
 *
 * __atomic_exchange_n(&var, new_val, ordering)
 *   — atomically stores new_val to *ptr and returns the OLD value
 *
 * __atomic_compare_exchange_n(&var, &expected, desired, weak, succ_ord, fail_ord)
 *   — atomically compares *ptr with *expected:
 *     if equal:     stores desired to *ptr, returns 1 (true)
 *     if not equal: writes *ptr to *expected, returns 0 (false)
 *
 * All operations are single-threaded. We verify correctness of return values
 * and final variable values, not concurrent behavior.
 *
 * Memory orderings used:
 *   __ATOMIC_SEQ_CST  (5) — strongest, sequentially consistent
 *   __ATOMIC_RELAXED  (0) — weakest, no ordering guarantees
 *   __ATOMIC_ACQUIRE  (2) — acquire semantics for loads
 *   __ATOMIC_RELEASE  (3) — release semantics for stores
 *
 * Type coverage: int, long, char
 * Note: long is 32-bit on i686, so test values must fit in 32-bit signed range.
 */

#include <stdio.h>

int main(void) {
    /* Test 1: Basic __atomic_exchange_n on int
     * Start with x=42, exchange with 99 using SEQ_CST.
     * __atomic_exchange_n returns the OLD value (42).
     * After the operation, x should be 99. */
    int x = 42;
    int old = __atomic_exchange_n(&x, 99, __ATOMIC_SEQ_CST);
    printf("exchange int: old=%d new=%d\n", old, x);

    /* Test 2: Exchange with RELAXED ordering
     * Start with y=100, exchange with 200 using RELAXED.
     * Verifies that the ordering parameter is correctly forwarded.
     * Old value should be 100, y should be 200. */
    int y = 100;
    old = __atomic_exchange_n(&y, 200, __ATOMIC_RELAXED);
    printf("exchange relaxed: old=%d new=%d\n", old, y);

    /* Test 3: Double exchange
     * Start with z=10, exchange with 20 (old1=10, z=20),
     * then exchange with 30 (old2=20, z=30).
     * Verifies chained exchange operations produce correct intermediate values. */
    int z = 10;
    int old1 = __atomic_exchange_n(&z, 20, __ATOMIC_SEQ_CST);
    int old2 = __atomic_exchange_n(&z, 30, __ATOMIC_SEQ_CST);
    printf("double exchange: old1=%d old2=%d z=%d\n", old1, old2, z);

    /* Test 4: CAS success case
     * Start with a=50, expected=50 (matches a), desired=75.
     * Since *ptr == *expected, CAS succeeds: a becomes 75, returns 1.
     * expected_a remains unchanged at 50 on success. */
    int a = 50;
    int expected_a = 50;
    int ret = __atomic_compare_exchange_n(&a, &expected_a, 75, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    printf("cmpxchg success: ret=%d a=%d expected=%d\n", ret, a, expected_a);

    /* Test 5: CAS failure case
     * Start with b=50, expected=99 (does NOT match b), desired=75.
     * Since *ptr (50) != *expected (99), CAS fails: b stays 50, returns 0.
     * expected_b is updated to the actual value of b (50). */
    int b = 50;
    int expected_b = 99;
    ret = __atomic_compare_exchange_n(&b, &expected_b, 75, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    printf("cmpxchg failure: ret=%d b=%d expected=%d\n", ret, b, expected_b);

    /* Test 6: CAS retry loop (single iteration since single-threaded)
     * Start with c=100, expected_c=100 (copy of c).
     * The while loop performs CAS with desired = expected_c + 10 = 110.
     * Since c == expected_c, the CAS succeeds on the first try, loop exits.
     * c should be 110 after the loop. */
    int c = 100;
    int expected_c = c;
    while (!__atomic_compare_exchange_n(&c, &expected_c, expected_c + 10, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        /* On failure, expected_c is updated with current value, retry */
    }
    printf("cmpxchg loop: c=%d\n", c);

    /* Test 7: CAS with ACQUIRE/RELAXED orderings
     * Start with d=42, expected_d=42 (matches d), desired=84.
     * Uses ACQUIRE for success ordering and RELAXED for failure ordering.
     * CAS should succeed: d becomes 84, returns 1. */
    int d = 42;
    int expected_d = 42;
    ret = __atomic_compare_exchange_n(&d, &expected_d, 84, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    printf("cmpxchg acq_rel: ret=%d d=%d\n", ret, d);

    /* Test 8: Exchange on long type
     * Start with lx=100000L, exchange with 200000L using SEQ_CST.
     * Old value should be 100000, lx should be 200000.
     * Values chosen to fit in both 32-bit and 64-bit long (i686 safe). */
    long lx = 100000L;
    long lold = __atomic_exchange_n(&lx, 200000L, __ATOMIC_SEQ_CST);
    printf("exchange long: old=%ld new=%ld\n", lold, lx);

    /* Test 9: CAS on char type
     * Start with cx=10, expected_cx=10 (matches cx), desired=20.
     * CAS should succeed: cx becomes 20, returns 1.
     * Verifies that the compiler handles sub-int-width atomic CAS.
     * Cast to int for printf %d format. */
    char cx = 10;
    char expected_cx = 10;
    ret = __atomic_compare_exchange_n(&cx, &expected_cx, 20, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    printf("cmpxchg char: ret=%d cx=%d\n", ret, (int)cx);

    /* Test 10: CAS failure then success
     * Start with e=5, expected_e=99 (does NOT match e).
     * First CAS fails: e stays 5, expected_e becomes 5 (writeback), returns 0.
     * Second CAS with corrected expected_e=5 succeeds: e becomes 77, returns 1.
     * This verifies the expected_ptr writeback semantics that enable retry loops. */
    int e = 5;
    int expected_e = 99;
    int fail_ret = __atomic_compare_exchange_n(&e, &expected_e, 77, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    /* expected_e is now 5 (the actual value of e) */
    int succ_ret = __atomic_compare_exchange_n(&e, &expected_e, 77, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    printf("cmpxchg retry: fail_ret=%d succ_ret=%d e=%d\n", fail_ret, succ_ret, e);

    return 0;
}
