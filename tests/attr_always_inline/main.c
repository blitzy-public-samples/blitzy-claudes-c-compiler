// Test: __attribute__((always_inline)) function attribute
//
// Verifies that:
// 1. __attribute__((always_inline)) is parsed and accepted on functions
// 2. __attribute__((__always_inline__)) underscore form is accepted
// 3. always_inline forces inlining regardless of optimization level
// 4. Chained always_inline calls (always_inline calling always_inline) work
// 5. always_inline and normal functions coexist correctly
//
// The always_inline attribute forces the compiler's inlining pass to inline
// the function at all call sites, overriding cost heuristics. In CCC this
// is enforced in src/passes/inline.rs where always_inline callees are exempt
// from budget limits and caller-size caps (C semantic requirement).

int printf(const char *fmt, ...);

// Scenario 1: static inline __attribute__((always_inline)) — common pattern
static inline __attribute__((always_inline))
int triple(int x) {
    return x * 3;
}

// Scenario 2: __attribute__((__always_inline__)) underscore form
static inline __attribute__((__always_inline__))
int add_ten(int x) {
    return x + 10;
}

// Scenario 3: always_inline with local variables and computation
static inline __attribute__((always_inline))
int sum_of_squares(int a, int b) {
    int sa = a * a;
    int sb = b * b;
    return sa + sb;
}

// Scenario 4: always_inline calling another always_inline function (chained)
static inline __attribute__((always_inline))
int square(int x) {
    return x * x;
}

static inline __attribute__((always_inline))
int sum_squares_chain(int a, int b) {
    return square(a) + square(b);
}

// Scenario 5: Normal function without attribute — baseline
int normal_mul(int a, int b) {
    return a * b;
}

int main(void) {
    int r1 = triple(7);              // 21
    int r2 = add_ten(15);            // 25
    int r3 = sum_of_squares(3, 4);   // 25
    int r4 = sum_squares_chain(5, 6); // 61
    int r5 = normal_mul(6, 7);       // 42

    printf("%d %d %d %d %d\n", r1, r2, r3, r4, r5);
    return 0;
}
