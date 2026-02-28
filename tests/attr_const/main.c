// Test: __attribute__((const)) function attribute
//
// Verifies that:
// 1. __attribute__((const)) is parsed and accepted on functions
// 2. __attribute__((__const__)) underscore form is accepted
// 3. Const functions (depend only on parameters, no memory reads) compile correctly
// 4. Const attribute is stricter than pure — function must not read global memory
// 5. Functions with const attribute execute correctly and return expected values
//
// __attribute__((const)) semantics (GCC extension):
// - Function return value depends ONLY on its parameters
// - Function must NOT read global memory or dereference pointer parameters
// - Function must NOT have side effects (no memory writes, no I/O)
// - Compiler may CSE calls with identical arguments
// - Stricter than __attribute__((pure)) which allows reading global memory

int printf(const char *fmt, ...);

// Scenario 1: basic const attribute — pure computation from single parameter
__attribute__((const))
int square(int x) {
    return x * x;
}

// Scenario 2: const attribute with multiple value parameters
__attribute__((const))
int add_mul(int a, int b, int c) {
    return (a + b) * c;
}

// Scenario 3: __const__ underscore form — GNU alternate spelling
__attribute__((__const__))
int negate(int x) {
    return -x;
}

// Scenario 4: const attribute on non-trivial arithmetic
__attribute__((const))
int triangle(int n) {
    return n * (n + 1) / 2;
}

// Scenario 5: Normal function — no attribute, baseline comparison
int normal_add(int a, int b) {
    return a + b;
}

int main(void) {
    int s = square(7);           // 7 * 7 = 49
    int am = add_mul(3, 4, 5);   // (3 + 4) * 5 = 35
    int n = negate(42);          // -42
    int t = triangle(10);        // 10 * 11 / 2 = 55
    int a = normal_add(20, 22);  // 42

    printf("%d %d %d %d %d\n", s, am, n, t, a);
    return 0;
}
