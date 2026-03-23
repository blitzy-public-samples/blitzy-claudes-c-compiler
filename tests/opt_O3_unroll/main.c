/* Test: -O3 loop unrolling verification.
 *
 * Verifies that the loop_unroll optimization pass (active only at -O3)
 * correctly unrolls a constant-bound loop while preserving computational
 * correctness. The loop in compute_sum() is an ideal unrolling candidate:
 *
 *   - Constant bound: 20 iterations (within the <=32 limit)
 *   - Post-unroll body: ~120 IR instructions (within the <=256 limit)
 *
 * The expected result (3180) must be produced regardless of whether the
 * loop is actually unrolled — this is a correctness invariant that holds
 * at -O0, -O2, and -O3. The test specifically exercises -O3 to ensure
 * the unrolling transformation does not introduce miscompilation.
 */

extern int printf(const char *fmt, ...);

/*
 * compute_sum: Contains a constant-bound loop with exactly 20 iterations.
 *
 * Loop body computes: sum += i*i + 3*i + 7
 *
 * Each iteration generates approximately 5-6 IR instructions:
 *   1. mul: i*i
 *   2. mul: 3*i
 *   3. add: i*i + 3*i
 *   4. add: (i*i + 3*i) + 7
 *   5. add: sum += result
 *   6. add: i++ (loop increment)
 *
 * At 20 iterations x ~6 instructions = ~120 IR instructions post-unroll,
 * well within the 256 instruction limit.
 *
 * Expected result via closed-form:
 *   sum(i^2 + 3i + 7, i=0..19)
 *     = sum(i^2) + 3*sum(i) + 20*7
 *     = (19*20*39)/6 + 3*(19*20)/2 + 140
 *     = 2470 + 570 + 140
 *     = 3180
 */
static int compute_sum(void) {
    int sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += i * i + 3 * i + 7;
    }
    return sum;
}

int main(void) {
    int result = compute_sum();
    printf("%d\n", result);
    return 0;
}
