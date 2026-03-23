// Test: __attribute__((unused)) on variables and functions
//
// Verifies that:
// 1. __attribute__((unused)) on local variables is parsed and accepted
// 2. __attribute__((unused)) on static functions is parsed and accepted
// 3. __attribute__((unused)) on function parameters is parsed and accepted
// 4. __attribute__((__unused__)) underscore form is accepted
// 5. Variables/functions with 'unused' attribute compile without warnings
//    even when they are not referenced
// 6. Variables with 'unused' attribute that ARE referenced still work correctly
//
// The 'unused' attribute tells the compiler that the declaration is
// intentionally unused, suppressing -Wunused-variable/-Wunused-function warnings.

int printf(const char *fmt, ...);

// Scenario 2: static function with __attribute__((unused)) — never called
// This must compile without -Wunused-function warnings.
__attribute__((unused))
static void unused_helper(void) {
    // Intentionally unused function — attribute suppresses warning
}

// Scenario 2b: another unused static function with underscore form
static void __attribute__((__unused__)) another_unused(void) {
    // Also intentionally unused
}

// Scenario 3: function parameter with __attribute__((unused))
// The 'ignored' parameter is intentionally not used.
int callback(int used_val, int __attribute__((unused)) ignored) {
    return used_val * 2;
}

// Scenario 6: normal function — no attribute, baseline
int add(int a, int b) {
    return a + b;
}

int main(void) {
    // Scenario 1: local variable with __attribute__((unused)) — not used
    int __attribute__((unused)) spare = 99;

    // Scenario 4: underscore form on local variable — not used
    int __attribute__((__unused__)) spare2 = 55;

    // Scenario 5: static array with unused attribute — not used
    static int __attribute__((unused)) table[] = {1, 2, 3};

    // Scenario 1b: local variable with unused attribute that IS used
    int __attribute__((unused)) val = 10;

    // Normal used variables
    int a = callback(val, 0);   // callback(10, 0) -> 10 * 2 = 20
    int b = add(a, 22);         // add(20, 22) -> 42
    int c = add(b, 8);          // add(42, 8) -> 50

    printf("%d %d %d\n", a, b, c);
    return 0;
}
