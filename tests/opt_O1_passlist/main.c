int printf(const char *fmt, ...);

static int add_and_fold(int x) {
    int k = 2 + 3;
    int m = k * 4;
    return x + m;
}

static int copy_chain(int x) {
    int a = x;
    int b = a;
    int c = b;
    return c * 2 + 1;
}

static int with_dead_code(int x) {
    int dead1 = x * x * x;
    int dead2 = dead1 + 42;
    int result = x + 7;
    return result;
}

int main(void) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += add_and_fold(i);
        sum += copy_chain(i);
        sum += with_dead_code(i);
    }
    printf("%d\n", sum);
    return 0;
}
