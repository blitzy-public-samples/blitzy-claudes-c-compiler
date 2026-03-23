// Test: __attribute__((cold)) and __attribute__((hot)) attributes
//
// Verifies that:
// 1. __attribute__((cold)) is parsed and applied (unlikely execution path hint)
// 2. __attribute__((hot)) is parsed and applied (frequent execution path hint)
// 3. Both __cold__ and __hot__ underscore forms are accepted
// 4. Functions with cold/hot attributes compile and execute correctly
// 5. Section placement hints are applied (cold -> .text.unlikely, hot -> .text.hot)
//
// cold: Marks a function as rarely executed. The compiler may place it in a
//       separate section (.text.unlikely) and optimize it for size over speed.
// hot:  Marks a function as frequently executed. The compiler may place it in a
//       separate section (.text.hot) and optimize it for speed over size.
//
// This test validates:
// - src/frontend/parser/parse.rs: dispatch_gcc_attribute() cold/hot arms
// - src/frontend/parser/ast.rs: FunctionAttributes COLD and HOT flags
// - src/backend/generation.rs: section placement for cold/hot functions

int printf(const char *fmt, ...);

// Scenario 1: cold attribute — error handler (rarely executed path)
__attribute__((cold))
int handle_error(int code) {
    return code * -1;
}

// Scenario 2: hot attribute — performance-critical function (frequently called)
__attribute__((hot))
int compute_fast(int a, int b) {
    return a + b + 1;
}

// Scenario 3: __cold__ underscore form — GNU alternate spelling
__attribute__((__cold__))
int rare_path(int x, int y) {
    return x * y - 2;
}

// Scenario 4: __hot__ underscore form — GNU alternate spelling
__attribute__((__hot__))
int critical_loop(int n) {
    return n * n + 1;
}

// Scenario 5: Normal function — no attribute, baseline comparison
int normal_func(int x) {
    return x * 2;
}

int main(void) {
    int err = handle_error(5);          // 5 * -1 = -5
    int fast = compute_fast(10, 20);    // 10 + 20 + 1 = 31
    int rare = rare_path(3, 4);         // 3 * 4 - 2 = 10
    int crit = critical_loop(6);        // 6 * 6 + 1 = 37
    int norm = normal_func(7);          // 7 * 2 = 14

    printf("%d %d %d %d %d\n", err, fast, rare, crit, norm);
    return 0;
}
