extern int printf(const char *fmt, ...);

int main(void) {
    int i, j;
    int total = 0;
    int n = 100;

    /* Test 1: 1000-iteration loop with single-nested VLA scope.
     * Each iteration allocates a 100-element int VLA (400 bytes) inside
     * a nested block scope. The stack pointer must be restored when the
     * block scope exits. If not restored, 1000 iterations would consume
     * ~400KB of stack and crash with a stack overflow. */
    for (i = 0; i < 1000; i++) {
        {
            int arr[n];
            for (j = 0; j < n; j++) {
                arr[j] = i + j;
            }
            total += arr[n - 1];
        }
    }

    /* Expected: sum of (i + 99) for i = 0..999
     * = sum(i, 0, 999) + 99 * 1000
     * = 499500 + 99000
     * = 598500 */
    printf("%d\n", total);

    /* Test 2: 1000-iteration loop with double-nested VLA scopes.
     * Each iteration allocates TWO VLAs in nested scopes: one in the
     * outer block and one in a deeper inner block. Both must be
     * deallocated at their respective scope exits. If either StackRestore
     * is missing, stack usage grows by 800 bytes per iteration. */
    int result = 0;
    for (i = 0; i < 1000; i++) {
        {
            int a[n];
            a[0] = i;
            {
                int b[n];
                b[0] = i * 2;
                result += a[0] + b[0];
            }
        }
    }

    /* Expected: sum of (i + 2*i) for i = 0..999
     * = sum of 3*i for i = 0..999
     * = 3 * 499500
     * = 1498500 */
    printf("%d\n", result);

    return 0;
}
