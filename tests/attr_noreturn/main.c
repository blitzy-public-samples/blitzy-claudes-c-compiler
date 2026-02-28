// Test: __attribute__((noreturn)) function attribute (GNU-style)
//
// Verifies that:
// 1. __attribute__((noreturn)) is parsed and accepted on functions
// 2. __attribute__((__noreturn__)) underscore form is accepted
// 3. noreturn on forward declaration propagates to definition
// 4. Compiler treats calls to noreturn functions as non-returning
//    (suppresses "control reaches end of non-void function" warning)
// 5. Functions marked noreturn that terminate via exit() work correctly
//
// This tests the GNU __attribute__ form specifically.
// The C11 _Noreturn keyword is tested separately in tests/noreturn/.
//
// __attribute__((noreturn)) semantics (GCC extension):
// - Function never returns to its caller
// - Compiler may eliminate dead code after noreturn calls
// - Compiler suppresses "missing return" warnings on paths ending in noreturn calls
// - Undefined behavior if a noreturn function actually returns

int printf(const char *fmt, ...);
void exit(int status);

// Scenario 1: __attribute__((noreturn)) on function definition
__attribute__((noreturn))
void fatal_exit(int code) {
    exit(code);
}

// Scenario 2: __attribute__((__noreturn__)) underscore form
__attribute__((__noreturn__))
void alt_exit(int code) {
    exit(code);
}

// Scenario 3: noreturn on forward declaration only
__attribute__((noreturn))
void fwd_exit(int code);

// Definition without repeating attribute — noreturn preserved from declaration
void fwd_exit(int code) {
    exit(code);
}

// Scenario 4: noreturn in control flow — suppresses "missing return" warning
// The compiler should NOT warn about missing return because the fallthrough
// path calls a noreturn function (fatal_exit), which the compiler knows
// never returns.
int get_value(int selector) {
    if (selector == 0) return 42;
    if (selector == 1) return 58;
    // This path calls a noreturn function — compiler accepts without warning
    fatal_exit(1);
}

// Scenario 5: Normal function — no attribute, baseline comparison
int compute(int a, int b) {
    return a + b;
}

int main(void) {
    int a = get_value(0);       // 42
    int b = get_value(1);       // 58
    int c = compute(19, 23);    // 42

    printf("%d %d %d\n", a, b, c);

    // Call noreturn function — program terminates here with code 0
    fwd_exit(0);

    // Unreachable code — compiler may optimize this away
    printf("unreachable\n");
    return 1;
}
