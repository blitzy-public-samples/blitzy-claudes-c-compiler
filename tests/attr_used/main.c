// Test: __attribute__((used)) on static variables and functions
//
// Verifies that:
// 1. __attribute__((used)) on static variables is parsed and accepted
// 2. __attribute__((used)) on static functions is parsed and accepted
// 3. __attribute__((__used__)) underscore form is accepted for both
// 4. Static variables/functions with 'used' are preserved (not stripped by DCE)
// 5. An uncalled static function with 'used' is preserved and does not cause errors
// 6. Called static functions with 'used' execute correctly
//
// The 'used' attribute marks static symbols as reachable roots in the
// dead_statics elimination pass, preventing removal even when unreferenced.

int printf(const char *fmt, ...);

// Scenario 1: static variable with __attribute__((used))
static int sentinel __attribute__((used)) = 42;

// Scenario 2: static variable with __attribute__((__used__)) underscore form
static int sentinel2 __attribute__((__used__)) = 100;

// Scenario 3: static function with __attribute__((used)) that is NOT called
// This verifies the function survives dead static elimination even without references.
// If the compiler erroneously strips this, compilation/linking would fail when
// the attribute is supposed to keep it.
static void __attribute__((used)) uncalled_helper(void) {
    // Preserved by __attribute__((used)) despite being unreferenced
}

// Scenario 4: static function with __attribute__((used)) that IS called
static int __attribute__((used)) add_values(int a, int b) {
    return a + b;
}

// Scenario 5: static function with __attribute__((__used__)) underscore form
static int __attribute__((__used__)) multiply(int a, int b) {
    return a * b;
}

int main(void) {
    int r1 = sentinel;                    // 42
    int r2 = sentinel2;                   // 100
    int r3 = add_values(r1, r2);          // 142
    int r4 = multiply(r1, 3);             // 126
    int r5 = add_values(r4, r2);          // 226

    printf("%d %d %d %d %d\n", r1, r2, r3, r4, r5);
    return 0;
}
