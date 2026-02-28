// Test: __attribute__((deprecated)) function and variable attribute
//
// Verifies that:
// 1. __attribute__((deprecated)) is parsed and accepted on functions
// 2. __attribute__((__deprecated__)) underscore form is accepted
// 3. __attribute__((deprecated("message"))) with optional message is accepted
// 4. __attribute__((deprecated)) is accepted on variables
// 5. Deprecated symbols still compile and execute correctly (warning is non-fatal)
// 6. deprecated on forward declaration propagates to definition
// 7. Non-deprecated symbols coexist with deprecated ones without issues
//
// __attribute__((deprecated)) semantics (GCC extension):
// - Compiler emits a warning when a deprecated symbol is used
// - Optional message string provides context: deprecated("use X instead")
// - Does NOT change runtime behavior — purely a diagnostic annotation
// - Warning is non-fatal unless combined with -Werror
// - Can be applied to functions, variables, and types

int printf(const char *fmt, ...);

// Scenario 1: basic deprecated function
__attribute__((deprecated))
int old_compute(int x) {
    return x * 2;
}

// Scenario 2: __deprecated__ underscore form
__attribute__((__deprecated__))
int legacy_add(int a, int b) {
    return a + b;
}

// Scenario 3: deprecated with message string
__attribute__((deprecated("use new_multiply instead")))
int old_multiply(int a, int b) {
    return a * b;
}

// Scenario 4: deprecated variable
__attribute__((deprecated))
int old_value = 100;

// Scenario 5: non-deprecated replacement function (baseline)
int new_compute(int x) {
    return x * 2;
}

// Scenario 6: deprecated on forward declaration, definition without attribute
__attribute__((deprecated))
int old_helper(int x);

int old_helper(int x) {
    return x + 8;
}

int main(void) {
    // Using deprecated functions — should trigger warnings after AAP changes
    int a = old_compute(21);       // 21 * 2 = 42
    int b = legacy_add(19, 23);    // 19 + 23 = 42
    int c = old_multiply(7, 6);    // 7 * 6 = 42

    // Using deprecated variable — should trigger warning after AAP changes
    int d = old_value;             // 100

    // Using non-deprecated function — no warnings
    int e = new_compute(21);       // 42

    // Using deprecated forward-declared function
    int f = old_helper(42);        // 42 + 8 = 50

    printf("%d %d %d %d %d %d\n", a, b, c, d, e, f);
    return 0;
}
