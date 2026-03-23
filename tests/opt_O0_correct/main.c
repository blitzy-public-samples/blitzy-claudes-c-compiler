int printf(const char *fmt, ...);

static int add(int a, int b) {
    return a + b;
}

static int mul(int a, int b) {
    return a * b;
}

static int fibonacci(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        int t = a + b;
        a = b;
        b = t;
    }
    return a;
}

static int sum_array(int *arr, int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += arr[i];
    return s;
}

struct Point {
    int x;
    int y;
};

static int dot(struct Point a, struct Point b) {
    return a.x * b.x + a.y * b.y;
}

int main(void) {
    /* 1. Basic arithmetic with many locals (stress stack allocation) */
    int a = 5;
    int b = 10;
    int c = add(a, b);
    int d = mul(c, 3);
    int e = d - b;
    printf("%d %d %d\n", c, d, e);

    /* 2. Loop with accumulation */
    int sum = 0;
    for (int i = 1; i <= 10; i++)
        sum += i;
    printf("%d\n", sum);

    /* 3. Function call chain - fibonacci */
    printf("%d\n", fibonacci(10));

    /* 4. Array operations */
    int arr[5] = {2, 4, 6, 8, 10};
    printf("%d\n", sum_array(arr, 5));

    /* 5. Struct pass-by-value and member access */
    struct Point p1 = {3, 4};
    struct Point p2 = {5, 6};
    printf("%d\n", dot(p1, p2));

    /* 6. Nested loops (matrix-like computation) */
    int grid = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            grid += i * j;
    printf("%d\n", grid);

    /* 7. Conditional chains (if-else) */
    int x = 7;
    if (x > 10)
        x = 1;
    else if (x > 5)
        x = 2;
    else
        x = 3;
    printf("%d\n", x);

    /* 8. While loop - Collatz conjecture steps */
    int n = 100;
    int steps = 0;
    while (n != 1) {
        if (n % 2 == 0)
            n = n / 2;
        else
            n = 3 * n + 1;
        steps++;
    }
    printf("%d\n", steps);

    /* 9. Pointer indirection */
    int v = 42;
    int *p = &v;
    *p = *p + 8;
    printf("%d\n", v);

    /* 10. Type conversion (long long to int) */
    long long big = 123456789LL;
    int small = (int)(big % 10000);
    printf("%d\n", small);

    return 0;
}
