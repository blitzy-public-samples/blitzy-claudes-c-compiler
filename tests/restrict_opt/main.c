extern int printf(const char *fmt, ...);

/* Test 1: GVN store-to-load forwarding with restrict-qualified pointers.
 * With restrict, GVN can forward the first load of *a to the second load
 * because storing through *b (restrict) cannot alias *a (restrict).
 * Without restrict, GVN must conservatively reload *a after *b = 100.
 * Either way, result is 14 because a and b genuinely don't alias. */
static int test_gvn_forward(int *restrict a, int *restrict b) {
    int val = *a;
    *b = 100;
    return val + *a;
}

/* Test 2: LICM safe load hoisting with restrict-qualified pointers.
 * With restrict, LICM can hoist the load of *factor out of the loop
 * because stores to arr[i] (through restrict arr) cannot alias *factor
 * (restrict). Without restrict, the load must stay inside the loop.
 * Either way, result is 45 because factor genuinely doesn't alias arr. */
static int test_licm_hoist(const int *restrict factor, int *restrict arr, int n) {
    int sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        arr[i] = arr[i] * (*factor);
        sum += arr[i];
    }
    return sum;
}

/* Test 3: Multiple restrict pointers — GVN can forward stored values
 * through restrict-qualified loads without reloading after intervening
 * stores through other restrict pointers.
 * *x = 10 can be forwarded past *y = 20 to the load of *x in *z = *x + *y.
 * Result: 30 */
static void test_multi_restrict(int *restrict x, int *restrict y, int *restrict z) {
    *x = 10;
    *y = 20;
    *z = *x + *y;
}

int main(void) {
    int a1 = 7;
    int b1 = 0;
    printf("%d\n", test_gvn_forward(&a1, &b1));

    int factor = 3;
    int arr[5];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    arr[4] = 5;
    printf("%d\n", test_licm_hoist(&factor, arr, 5));

    int x3 = 0, y3 = 0, z3 = 0;
    test_multi_restrict(&x3, &y3, &z3);
    printf("%d\n", z3);

    return 0;
}
