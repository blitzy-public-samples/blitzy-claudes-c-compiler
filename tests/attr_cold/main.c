// Test: __attribute__((cold)) function attribute
//
// Verifies that:
// 1. __attribute__((cold)) is parsed and applied (unlikely execution path hint)
// 2. __attribute__((__cold__)) underscore form is accepted
// 3. Cold-attributed functions compile and execute correctly
// 4. Section placement hint may place cold code in .text.unlikely
//
// cold: Marks a function as rarely executed. The compiler may place it in a
//       separate section (.text.unlikely) and optimize it for size over speed.

int printf(const char *fmt, ...);

// Scenario 1: cold attribute — error handler (rarely executed path)
__attribute__((cold))
int handle_error(int code) {
    return code * -1;
}

// Scenario 2: __cold__ underscore form
__attribute__((__cold__))
int rare_path(int x, int y) {
    return x * y - 2;
}

// Scenario 3: normal function for comparison
int normal_func(int x) {
    return x * 2;
}

int main(void) {
    int err = handle_error(5);     // 5 * -1 = -5
    int rare = rare_path(3, 4);    // 3 * 4 - 2 = 10
    int norm = normal_func(7);     // 7 * 2 = 14

    printf("%d %d %d\n", err, rare, norm);
    return 0;
}
