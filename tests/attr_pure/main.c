// Test: __attribute__((pure)) function attribute
//
// Verifies that:
// 1. __attribute__((pure)) is parsed and accepted on functions
// 2. __attribute__((__pure__)) underscore form is accepted
// 3. Pure functions (depend on parameters and global state, no side effects) compile correctly
// 4. Pure attribute allows reading global memory (unlike const attribute)
// 5. Functions with pure attribute execute correctly and return expected values
//
// __attribute__((pure)) semantics (GCC extension):
// - Function return value depends on parameters AND global memory state
// - Function MAY read global memory and dereference pointer parameters
// - Function must NOT have side effects (no memory writes, no I/O)
// - Compiler may eliminate redundant calls if global state is unchanged
// - Less strict than __attribute__((const)) which forbids reading global memory

int printf(const char *fmt, ...);

// Global variable read by pure function — distinguishes pure from const
int global_factor = 3;

// Scenario 1: basic pure attribute — pure computation from single parameter
__attribute__((pure))
int double_val(int x) {
    return x * 2;
}

// Scenario 2: pure function reading global state — key distinction from const
__attribute__((pure))
int scale(int x) {
    return x * global_factor;
}

// Scenario 3: __pure__ underscore form — GNU alternate spelling
__attribute__((__pure__))
int cube(int x) {
    return x * x * x;
}

// Scenario 4: pure attribute with multiple parameters
__attribute__((pure))
int weighted_sum(int a, int b, int w) {
    return a * w + b * (100 - w);
}

// Scenario 5: Normal function — no attribute, baseline comparison
int plain_add(int a, int b) {
    return a + b;
}

int main(void) {
    int d = double_val(21);            // 21 * 2 = 42
    int s = scale(7);                  // 7 * global_factor(3) = 21
    int c = cube(4);                   // 4 * 4 * 4 = 64
    int w = weighted_sum(10, 20, 60);  // 10 * 60 + 20 * 40 = 1400
    int p = plain_add(30, 12);         // 30 + 12 = 42

    printf("%d %d %d %d %d\n", d, s, c, w, p);
    return 0;
}
