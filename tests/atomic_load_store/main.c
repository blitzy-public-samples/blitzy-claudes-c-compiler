/*
 * Integration test: C11 _Atomic load/store operations
 *
 * Verifies that CCC correctly tracks the _Atomic type qualifier keyword
 * and lowers __atomic_load_n / __atomic_store_n GCC-style atomic builtins
 * across all 4 target architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * __atomic_load_n(&var, ordering)
 *   — atomically loads and returns the value at *ptr
 *
 * __atomic_store_n(&var, value, ordering)
 *   — atomically stores value to *ptr, returns void
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
 * Type coverage: _Atomic int, _Atomic long
 * Note: long is 32-bit on i686, so test values must fit in 32-bit signed range.
 */

#include <stdio.h>

int main(void) {
    /* Test 1: Load initial value of _Atomic int
     * Declare _Atomic int x = 42, atomically load with SEQ_CST.
     * __atomic_load_n returns the current value (42). */
    _Atomic int x = 42;
    int v = __atomic_load_n(&x, __ATOMIC_SEQ_CST);
    printf("load initial: v=%d\n", v);

    /* Test 2: Store then load on _Atomic int
     * Atomically store 99 to x, then load it back.
     * The loaded value must be 99. */
    __atomic_store_n(&x, 99, __ATOMIC_SEQ_CST);
    v = __atomic_load_n(&x, __ATOMIC_SEQ_CST);
    printf("store_load: v=%d\n", v);

    /* Test 3: Overwrite test
     * Store 100 to x, then immediately store 200 to x.
     * The final loaded value must be 200 (last store wins). */
    __atomic_store_n(&x, 100, __ATOMIC_SEQ_CST);
    __atomic_store_n(&x, 200, __ATOMIC_SEQ_CST);
    v = __atomic_load_n(&x, __ATOMIC_SEQ_CST);
    printf("overwrite: v=%d\n", v);

    /* Test 4: RELAXED ordering
     * Store and load with __ATOMIC_RELAXED (weakest ordering).
     * Single-threaded, so value must still be correct. */
    _Atomic int y = 0;
    __atomic_store_n(&y, 77, __ATOMIC_RELAXED);
    v = __atomic_load_n(&y, __ATOMIC_RELAXED);
    printf("relaxed: v=%d\n", v);

    /* Test 5: RELEASE/ACQUIRE ordering pair
     * Store with __ATOMIC_RELEASE, load with __ATOMIC_ACQUIRE.
     * This is the typical producer-consumer pattern ordering. */
    _Atomic int z = 0;
    __atomic_store_n(&z, 55, __ATOMIC_RELEASE);
    v = __atomic_load_n(&z, __ATOMIC_ACQUIRE);
    printf("release_acquire: v=%d\n", v);

    /* Test 6: _Atomic long initial load
     * Declare _Atomic long with value 100000 (fits in 32-bit for i686).
     * Load with SEQ_CST and verify. */
    _Atomic long lx = 100000L;
    long lv = __atomic_load_n(&lx, __ATOMIC_SEQ_CST);
    printf("long initial: lv=%ld\n", lv);

    /* Test 7: _Atomic long store then load
     * Store 500000 to lx (fits in 32-bit for i686), load and verify. */
    __atomic_store_n(&lx, 500000L, __ATOMIC_SEQ_CST);
    lv = __atomic_load_n(&lx, __ATOMIC_SEQ_CST);
    printf("long store_load: lv=%ld\n", lv);

    /* Test 8: Multiple _Atomic int variables
     * Declare 3 separate _Atomic int variables, store distinct values,
     * load all 3 back and verify each independently. */
    _Atomic int a = 0;
    _Atomic int b = 0;
    _Atomic int c = 0;
    __atomic_store_n(&a, 10, __ATOMIC_SEQ_CST);
    __atomic_store_n(&b, 20, __ATOMIC_SEQ_CST);
    __atomic_store_n(&c, 30, __ATOMIC_SEQ_CST);
    int va = __atomic_load_n(&a, __ATOMIC_SEQ_CST);
    int vb = __atomic_load_n(&b, __ATOMIC_SEQ_CST);
    int vc = __atomic_load_n(&c, __ATOMIC_SEQ_CST);
    printf("multi: a=%d b=%d c=%d\n", va, vb, vc);

    /* Test 9: Sequential store-load loop
     * Loop i from 1 to 5: store i*10 to _Atomic int s, load it back
     * and accumulate into sum. sum = 10+20+30+40+50 = 150. */
    _Atomic int s = 0;
    int sum = 0;
    for (int i = 1; i <= 5; i++) {
        __atomic_store_n(&s, i * 10, __ATOMIC_SEQ_CST);
        sum += __atomic_load_n(&s, __ATOMIC_SEQ_CST);
    }
    printf("sequential sum: sum=%d\n", sum);

    return 0;
}
