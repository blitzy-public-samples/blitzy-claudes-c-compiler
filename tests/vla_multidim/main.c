extern int printf(const char *fmt, ...);

/* Helper: sum all elements of a 2D VLA.
 * Tests passing a multi-dimensional VLA to a function.
 * The inner dimension cols is part of the element type and
 * is used for stride computation in arr[i][j]. */
static int sum_2d(int rows, int cols, int arr[rows][cols]) {
    int i, j;
    int s = 0;
    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++)
            s += arr[i][j];
    return s;
}

int main(void) {
    int n = 3;
    int m = 4;

    /* Test 1: 2D VLA declaration and element access.
     * int arr[n][m] with runtime n=3, m=4.
     * Fill sequentially 1..12, then access arr[2][3].
     * Row 0: {1, 2, 3, 4}
     * Row 1: {5, 6, 7, 8}
     * Row 2: {9, 10, 11, 12}
     * arr[2][3] = 12 */
    int arr[n][m];
    int i, j;
    int val = 1;
    for (i = 0; i < n; i++)
        for (j = 0; j < m; j++)
            arr[i][j] = val++;
    printf("%d\n", arr[2][3]);

    /* Test 2: sizeof for 2D VLA.
     * sizeof(arr) evaluates at runtime to n * m * sizeof(int).
     * 3 * 4 * 4 = 48 */
    printf("%d\n", (int)sizeof(arr));

    /* Test 3: sizeof for VLA row.
     * sizeof(arr[0]) evaluates at runtime to m * sizeof(int).
     * 4 * 4 = 16 */
    printf("%d\n", (int)sizeof(arr[0]));

    /* Test 4: Stride verification.
     * arr[1][0] should be 5. This verifies that the stride for
     * the first dimension is correctly computed as m * sizeof(int)
     * = 16 bytes, placing row 1 at the correct offset. */
    printf("%d\n", arr[1][0]);

    /* Test 5: 3D VLA declaration and element access.
     * int cube[n][m][p] with n=3, m=4, p=2.
     * Fill with encoding: i*100 + j*10 + k.
     * cube[2][3][1] = 2*100 + 3*10 + 1 = 231 */
    int p = 2;
    int cube[n][m][p];
    {
        int ci, cj, ck;
        for (ci = 0; ci < n; ci++)
            for (cj = 0; cj < m; cj++)
                for (ck = 0; ck < p; ck++)
                    cube[ci][cj][ck] = ci * 100 + cj * 10 + ck;
    }
    printf("%d\n", cube[2][3][1]);

    /* Test 6: sizeof for 3D VLA.
     * sizeof(cube) = n * m * p * sizeof(int).
     * 3 * 4 * 2 * 4 = 96 */
    printf("%d\n", (int)sizeof(cube));

    /* Test 7: Sum of all elements of 2D VLA via function call.
     * Tests passing multi-dim VLA to function with VLA parameter.
     * Sum = 1+2+3+...+12 = (12*13)/2 = 78 */
    printf("%d\n", sum_2d(n, m, arr));

    return 0;
}
