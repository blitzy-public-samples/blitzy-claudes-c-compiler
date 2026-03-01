extern int printf(const char *fmt, ...);

/* Test 1: Basic VLA parameter - array with size from another parameter.
 * Per C11 §6.7.6.3, a function parameter declared as an array with
 * a non-constant size expression is a VLA parameter. The outermost
 * dimension decays to a pointer, but the size expression is parsed. */
static int sum_array(int n, int arr[n]) {
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        s += arr[i];
    }
    return s;
}

/* Test 2: VLA [*] syntax in prototype, [n] in definition.
 * Per C11 §6.7.6.3, [*] in a function declaration (not a definition)
 * denotes a VLA of unspecified size. The definition provides [n].
 * This tests that the parser accepts [*] abstract VLA syntax and
 * that the prototype is compatible with the definition. */
static int last_element(int, int [*]);
static int last_element(int n, int arr[n]) {
    return arr[n - 1];
}

/* Test 3: Multi-dimensional VLA parameter.
 * int mat[rows][cols] decays to int (*)[cols] — a pointer to a VLA row.
 * The inner dimension cols is part of the element type and is used
 * for address computation in mat[r][c]. */
static int mat_element(int rows, int cols, int mat[rows][cols], int r, int c) {
    return mat[r][c];
}

/* Test 4: VLA parameter with 'static' keyword.
 * Per C11 §6.7.6.3p7, [static n] in a function parameter declaration
 * specifies that the caller guarantees at least n elements.
 * The parser's skip_array_qualifiers() skips the 'static' token
 * before parsing the size expression. */
static int third_element(int n, int arr[static n]) {
    return arr[2];
}

/* Test 5: Multiple VLA parameters in one function.
 * Both arrays use the same size parameter n. */
static int dot_product(int n, int a[n], int b[n]) {
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        s += a[i] * b[i];
    }
    return s;
}

/* Test 6: VLA parameter with size expression.
 * The size expression n + 0 is evaluated at runtime. The parser
 * calls parse_expr() which handles the full expression, not just
 * a simple identifier. */
static int sum_expr_param(int n, int arr[n + 0]) {
    int s = 0;
    int i;
    for (i = 0; i < n; i++) {
        s += arr[i];
    }
    return s;
}

int main(void) {
    int a[5];
    a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40; a[4] = 50;

    /* Test 1: basic VLA parameter, sum of 5 elements
     * 10 + 20 + 30 + 40 + 50 = 150 */
    printf("%d\n", sum_array(5, a));

    /* Test 2: [*] syntax in prototype, last element
     * a[4] = 50 */
    printf("%d\n", last_element(5, a));

    /* Test 3: multi-dimensional VLA parameter
     * mat[1][2] = 6 */
    int mat[2][3];
    mat[0][0] = 1; mat[0][1] = 2; mat[0][2] = 3;
    mat[1][0] = 4; mat[1][1] = 5; mat[1][2] = 6;
    printf("%d\n", mat_element(2, 3, mat, 1, 2));

    /* Test 4: static VLA parameter
     * a[2] = 30 */
    printf("%d\n", third_element(5, a));

    /* Test 5: multiple VLA parameters, dot product
     * 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32 */
    int b[3];
    b[0] = 1; b[1] = 2; b[2] = 3;
    int c[3];
    c[0] = 4; c[1] = 5; c[2] = 6;
    printf("%d\n", dot_product(3, b, c));

    /* Test 6: expression in VLA parameter size
     * 100 + 200 + 300 = 600 */
    int d[3];
    d[0] = 100; d[1] = 200; d[2] = 300;
    printf("%d\n", sum_expr_param(3, d));

    return 0;
}
