extern int printf(const char *fmt, ...);

int main(void) {
    int i;
    int n = 10;

    /* Test 1: Basic VLA stack allocation with runtime size.
     * Allocate int arr[n] where n=10 (runtime variable, not const).
     * Fill with arr[i] = (i+1)*10 and sum all elements.
     * Verifies that DynAlloca allocates correct amount of stack
     * space and element read/write works correctly. */
    int arr[n];
    for (i = 0; i < n; i++) {
        arr[i] = (i + 1) * 10;
    }
    int sum = 0;
    for (i = 0; i < n; i++) {
        sum += arr[i];
    }
    /* Expected: 10+20+30+40+50+60+70+80+90+100
     * = 10 * (1+2+...+10) = 10 * 55 = 550 */
    printf("%d\n", sum);

    /* Test 2: Runtime sizeof evaluation for VLA.
     * sizeof(arr) must evaluate at runtime to n * sizeof(int).
     * With n=10 and sizeof(int)=4 on all targets: 10 * 4 = 40.
     * This verifies that lower_sizeof -> get_vla_sizeof returns
     * the runtime Value stored in info.vla_size, not a constant. */
    printf("%d\n", (int)sizeof(arr));

    /* Test 3: Second VLA with different runtime size.
     * Allocate int brr[m] where m=20 to verify that VLA size
     * is truly dynamic (not hardcoded from first allocation).
     * Fill with brr[i] = i and sum all elements. */
    int m = 20;
    int brr[m];
    for (i = 0; i < m; i++) {
        brr[i] = i;
    }
    int sum2 = 0;
    for (i = 0; i < m; i++) {
        sum2 += brr[i];
    }
    /* Expected: 0+1+2+...+19 = 19*20/2 = 190 */
    printf("%d\n", sum2);

    /* Test 4: Runtime sizeof for second VLA.
     * sizeof(brr) = m * sizeof(int) = 20 * 4 = 80.
     * Confirms runtime sizeof works for multiple VLAs. */
    printf("%d\n", (int)sizeof(brr));

    /* Test 5: VLA boundary element access.
     * Access arr[n-1] (last element) to verify full allocation. */
    printf("%d\n", arr[n - 1]);

    return 0;
}
