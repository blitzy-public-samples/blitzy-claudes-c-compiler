// Test: __attribute__((warn_unused_result)) function attribute
//
// Verifies that:
// 1. __attribute__((warn_unused_result)) is parsed and accepted on functions
// 2. __attribute__((__warn_unused_result__)) underscore form is accepted
// 3. Functions with warn_unused_result execute correctly when return values are used
// 4. Discarding the return value of a warn_unused_result function triggers a warning
// 5. Functions without the attribute do not trigger warnings when result is discarded
//
// __attribute__((warn_unused_result)) semantics (GCC extension):
// - Compiler emits a warning when the return value is discarded by caller
// - Does NOT change runtime behavior — purely a diagnostic annotation
// - Common use: marking allocators, I/O functions, error-returning functions
// - Warning is non-fatal unless combined with -Werror

int printf(const char *fmt, ...);

// Scenario 1: basic warn_unused_result — return value will be USED
__attribute__((warn_unused_result))
int compute(int x) {
    return x * 2;
}

// Scenario 2: __warn_unused_result__ underscore form — return value will be USED
__attribute__((__warn_unused_result__))
int multiply(int a, int b) {
    return a * b;
}

// Scenario 3: allocator-like function — common real-world use case
__attribute__((warn_unused_result))
int allocate(int size) {
    return size;
}

// Scenario 5: Normal function — no attribute, baseline comparison
int plain_add(int a, int b) {
    return a + b;
}

int main(void) {
    // These calls USE the return value — no warning expected
    int r1 = compute(21);          // 21 * 2 = 42
    int r2 = multiply(7, 6);       // 7 * 6 = 42
    int r3 = allocate(42);         // 42

    // Scenario 4: These calls DISCARD the return value
    // After AAP implementation, the compiler should emit warn_unused_result warnings
    // for these calls on stderr. The program still compiles and runs correctly.
    compute(10);     // Return value discarded — should warn
    multiply(3, 4);  // Return value discarded — should warn

    // No attribute — discarding is fine, no warning expected
    plain_add(1, 2);

    printf("%d %d %d\n", r1, r2, r3);
    return 0;
}
