// Test: __attribute__((hot)) function attribute
//
// Verifies that:
// 1. __attribute__((hot)) is parsed and applied (frequent execution path hint)
// 2. __attribute__((__hot__)) underscore form is accepted
// 3. Hot-attributed functions compile and execute correctly
// 4. Section placement hint may place hot code in .text.hot
//
// hot: Marks a function as frequently executed. The compiler may place it in a
//      separate section (.text.hot) and optimize it for speed over size.

int printf(const char *fmt, ...);

// Scenario 1: hot attribute — performance-critical function
__attribute__((hot))
int compute_fast(int a, int b) {
    return a + b + 1;
}

// Scenario 2: __hot__ underscore form
__attribute__((__hot__))
int critical_loop(int n) {
    return n * n + 1;
}

// Scenario 3: normal function for comparison
int normal_func(int x) {
    return x * 2;
}

int main(void) {
    int fast = compute_fast(10, 20);    // 10 + 20 + 1 = 31
    int crit = critical_loop(6);        // 6 * 6 + 1 = 37
    int norm = normal_func(7);          // 7 * 2 = 14

    printf("%d %d %d\n", fast, crit, norm);
    return 0;
}
