// Test: __attribute__((noinline)) function attribute
//
// Verifies that:
// 1. __attribute__((noinline)) is parsed and accepted on functions
// 2. __attribute__((__noinline__)) underscore form is accepted
// 3. noinline functions produce correct results (not inlined but callable)
// 4. noinline function pointers are valid (separate symbols exist)
// 5. noinline and normal functions coexist correctly
//
// The noinline attribute prevents the compiler's inlining pass from inlining
// the function at call sites, even at high optimization levels (-O2, -O3).
// This is verified by the CCC inline pass (src/passes/inline.rs:982-983):
//   if func.is_noinline { continue; }

int printf(const char *fmt, ...);

// Scenario 1: __attribute__((noinline)) on simple function
// Would normally be an inlining candidate due to small size
__attribute__((noinline))
int square(int x) {
    return x * x;
}

// Scenario 2: __attribute__((__noinline__)) underscore form
__attribute__((__noinline__))
int cube(int x) {
    return x * x * x;
}

// Scenario 3: noinline on function with loop body
__attribute__((noinline))
int sum_range(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++)
        total += i;
    return total;
}

// Scenario 4: Normal function without noinline — baseline comparison
int normal_add(int a, int b) {
    return a + b;
}

int main(void) {
    int r1 = square(7);         // 49
    int r2 = cube(3);           // 27
    int r3 = sum_range(10);     // 55
    int r4 = normal_add(20, 22); // 42

    printf("%d %d %d %d\n", r1, r2, r3, r4);

    // Function pointer to noinline function — must be a valid callable symbol
    int (*fp)(int) = square;
    printf("%d\n", fp(5));      // 25

    return 0;
}
