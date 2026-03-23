extern int printf(const char *fmt, ...);

/* Test 1: restrict-qualified pointer parameters.
 * Per C11 §6.7.3.1, restrict is a type qualifier on pointer types.
 * The compiler must parse 'int * restrict p' and generate correct code. */
static int test_restrict_params(int * restrict a, int * restrict b, int n) {
    int sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        a[i] = i + 1;
        b[i] = a[i] * 2;
        sum += b[i];
    }
    return sum;
}

/* Test 2: __restrict alternative spelling (GNU extension).
 * CCC maps __restrict to TokenKind::Restrict (token.rs:422).
 * Must be accepted and function identically to restrict. */
static int test_gnu_restrict(int * __restrict p, int * __restrict q) {
    *p = 42;
    *q = 58;
    return *p + *q;
}

/* Test 3: __restrict__ alternative spelling (GNU extension).
 * CCC maps __restrict__ to TokenKind::Restrict (token.rs:422).
 * Must be accepted and function identically to restrict. */
static int test_gnu_restrict_underscores(int * __restrict__ x, int * __restrict__ y) {
    *x = 10;
    *y = 20;
    return *x + *y;
}

/* Test 4: restrict-qualified local pointer variable.
 * A local pointer declared with restrict is valid C11.
 * The compiler must accept this and generate correct code. */
static int test_restrict_local(int *arr, int n) {
    int * restrict p = arr;
    int sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        sum += p[i];
    }
    return sum;
}

/* Test 5: const restrict pointer parameter.
 * Per C11, const and restrict can both qualify the same pointer.
 * The compiler must accept 'const int * restrict' and preserve const semantics. */
static int test_const_restrict(const int * restrict a, const int * restrict b, int n) {
    int sum = 0;
    int i;
    for (i = 0; i < n; i++) {
        sum += a[i] + b[i];
    }
    return sum;
}

/* Test 6: restrict in typedef context.
 * A typedef can include restrict on the pointer type. */
typedef int * restrict restrict_int_ptr;
static int test_restrict_typedef(restrict_int_ptr p) {
    *p = 77;
    return *p;
}

/* Test 7: volatile restrict pointer.
 * Both volatile and restrict can qualify a pointer together. */
static int test_volatile_restrict(volatile int * restrict p) {
    *p = 99;
    return *p;
}

int main(void) {
    int arr1[5];
    int arr2[5];

    /* Test 1: restrict-qualified pointer parameters */
    /* sum = 2 + 4 + 6 + 8 + 10 = 30 */
    printf("%d\n", test_restrict_params(arr1, arr2, 5));

    /* Test 2: __restrict spelling */
    int v1, v2;
    /* 42 + 58 = 100 */
    printf("%d\n", test_gnu_restrict(&v1, &v2));

    /* Test 3: __restrict__ spelling */
    int v3, v4;
    /* 10 + 20 = 30 */
    printf("%d\n", test_gnu_restrict_underscores(&v3, &v4));

    /* Test 4: restrict-qualified local pointer */
    int data[4];
    data[0] = 1;
    data[1] = 2;
    data[2] = 3;
    data[3] = 4;
    /* 1 + 2 + 3 + 4 = 10 */
    printf("%d\n", test_restrict_local(data, 4));

    /* Test 5: const restrict pointer parameter */
    int ca[3];
    int cb[3];
    ca[0] = 10; ca[1] = 20; ca[2] = 30;
    cb[0] = 1; cb[1] = 2; cb[2] = 3;
    /* (10+1) + (20+2) + (30+3) = 11 + 22 + 33 = 66 */
    printf("%d\n", test_const_restrict(ca, cb, 3));

    /* Test 6: restrict in typedef */
    int tv = 0;
    /* 77 */
    printf("%d\n", test_restrict_typedef(&tv));

    /* Test 7: volatile restrict pointer */
    int vr = 0;
    /* 99 */
    printf("%d\n", test_volatile_restrict(&vr));

    return 0;
}
