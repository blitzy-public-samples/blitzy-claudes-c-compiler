/*
 * Integration test: __atomic_fetch_add and __atomic_add_fetch builtins
 *
 * Verifies that CCC correctly lowers GCC-style atomic read-modify-write
 * add operations across all 4 target architectures (x86-64, AArch64,
 * RISC-V 64, i686).
 *
 * __atomic_fetch_add(&var, val, ordering) — returns the OLD value before add
 * __atomic_add_fetch(&var, val, ordering) — returns the NEW value after add
 *
 * All operations are single-threaded. We verify correctness of return values
 * and final variable values, not concurrent behavior.
 *
 * Memory orderings used:
 *   __ATOMIC_SEQ_CST  (5) — strongest, sequentially consistent
 *   __ATOMIC_RELAXED  (0) — weakest, no ordering guarantees
 *
 * Type coverage: int, long, char
 * Note: long is 32-bit on i686, so test values must fit in 32-bit signed range.
 */

#include <stdio.h>

int main(void) {
    /* Test 1: Basic __atomic_fetch_add on int
     * Start with x=10, fetch_add 5 with SEQ_CST.
     * __atomic_fetch_add returns the OLD value (10).
     * After the operation, x should be 15. */
    int x = 10;
    int old = __atomic_fetch_add(&x, 5, __ATOMIC_SEQ_CST);
    printf("fetch_add int: old=%d new=%d\n", old, x);

    /* Test 2: Basic __atomic_add_fetch on int
     * Start with y=20, add_fetch 7 with SEQ_CST.
     * __atomic_add_fetch returns the NEW value (27).
     * After the operation, y should also be 27. */
    int y = 20;
    int result = __atomic_add_fetch(&y, 7, __ATOMIC_SEQ_CST);
    printf("add_fetch int: result=%d y=%d\n", result, y);

    /* Test 3: __atomic_fetch_add with RELAXED ordering
     * Start with z=100, fetch_add 50 with RELAXED.
     * Verifies that the ordering parameter is correctly forwarded.
     * Old value should be 100, z should be 150. */
    int z = 100;
    old = __atomic_fetch_add(&z, 50, __ATOMIC_RELAXED);
    printf("fetch_add relaxed: old=%d new=%d\n", old, z);

    /* Test 4: Sequential fetch_add accumulation
     * Start with w=0, perform three sequential fetch_adds: +1, +2, +3.
     * Final w should be 0+1+2+3 = 6. */
    int w = 0;
    __atomic_fetch_add(&w, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&w, 2, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&w, 3, __ATOMIC_SEQ_CST);
    printf("sequential: w=%d\n", w);

    /* Test 5: Negative value fetch_add
     * Start with n=50, fetch_add -30 with SEQ_CST.
     * Old value should be 50, n should be 20.
     * Verifies that negative operands work correctly. */
    int n = 50;
    old = __atomic_fetch_add(&n, -30, __ATOMIC_SEQ_CST);
    printf("negative: old=%d new=%d\n", old, n);

    /* Test 6: add_fetch accumulation
     * Start with a=0.
     * First add_fetch of 10 returns 10 (new value), a becomes 10.
     * Second add_fetch of 20 returns 30 (new value), a becomes 30. */
    int a = 0;
    int r1 = __atomic_add_fetch(&a, 10, __ATOMIC_SEQ_CST);
    int r2 = __atomic_add_fetch(&a, 20, __ATOMIC_SEQ_CST);
    printf("add_fetch accum: r1=%d r2=%d a=%d\n", r1, r2, a);

    /* Test 7: long type fetch_add
     * Start with lx=100000L, fetch_add 200000L with SEQ_CST.
     * Old value should be 100000, lx should be 300000.
     * Values chosen to fit in both 32-bit and 64-bit long (i686 safe). */
    long lx = 100000L;
    long lold = __atomic_fetch_add(&lx, 200000L, __ATOMIC_SEQ_CST);
    printf("fetch_add long: old=%ld new=%ld\n", lold, lx);

    /* Test 8: char type fetch_add
     * Start with cx=10, fetch_add 20 on a char.
     * Old value should be 10, cx should be 30.
     * Verifies that the compiler handles sub-int-width atomic RMW.
     * Cast to int for printf %d format. */
    char cx = 10;
    char cold = __atomic_fetch_add(&cx, 20, __ATOMIC_SEQ_CST);
    printf("fetch_add char: old=%d new=%d\n", (int)cold, (int)cx);

    return 0;
}
