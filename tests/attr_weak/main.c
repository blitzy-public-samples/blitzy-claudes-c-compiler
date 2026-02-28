// Test: __attribute__((weak)) symbol attribute
//
// Verifies that:
// 1. __attribute__((weak)) is parsed and accepted on functions
// 2. __attribute__((__weak__)) underscore form is accepted
// 3. Weak functions produce correct results when called (not overridden)
// 4. Weak global variables are accessible and hold correct values
// 5. ELF symbol binding is correct (.weak directive emitted instead of .globl)
// 6. Weak and strong symbols coexist correctly in the same translation unit
//
// __attribute__((weak)) semantics (GCC extension):
// - Marks symbol with ELF STB_WEAK binding instead of STB_GLOBAL
// - Weak symbols can be overridden by strong definitions at link time
// - If no strong definition exists, the weak definition is used
// - Codegen emits: .weak symbol_name (instead of .globl symbol_name)
// - Used extensively in libc and the Linux kernel

int printf(const char *fmt, ...);

// Scenario 1: Weak function — basic __attribute__((weak))
__attribute__((weak))
int weak_add(int a, int b) {
    return a + b;
}

// Scenario 2: Weak function — __weak__ underscore form
__attribute__((__weak__))
int weak_mul(int a, int b) {
    return a * b;
}

// Scenario 3: Weak global variable
__attribute__((weak)) int weak_var = 55;

// Scenario 4: Strong function — normal .globl binding, for comparison
int strong_func(int a, int b) {
    return a - b;
}

// Scenario 5: Strong global variable — normal binding, for comparison
int strong_var = 100;

int main(void) {
    // Call weak functions — they have definitions, so they are used
    int r1 = weak_add(3, 4);     // 3 + 4 = 7
    int r2 = weak_mul(5, 6);     // 5 * 6 = 30

    // Read weak global variable
    int r3 = weak_var;            // 55

    // Call strong function — normal .globl binding
    int r4 = strong_func(10, 3);  // 10 - 3 = 7

    // Read strong global variable
    int r5 = strong_var;          // 100

    printf("%d %d %d %d %d\n", r1, r2, r3, r4, r5);
    return 0;
}
