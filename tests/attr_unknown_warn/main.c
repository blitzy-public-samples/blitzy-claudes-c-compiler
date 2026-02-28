// Test: Unknown attribute -Wattributes warning
//
// Verifies that:
// 1. The compiler emits -Wattributes warnings for unrecognized __attribute__ names
// 2. Code with unknown attributes still compiles and runs correctly
// 3. -Wno-attributes suppresses the warnings (test by compiling with that flag)
//
// Compilation scenarios:
//   Default:           ccc -o test main.c
//     -> -Wattributes warnings emitted for unknown attrs, compilation succeeds
//   Suppress warnings: ccc -Wno-attributes -o test main.c
//     -> No warnings emitted, compilation succeeds
//   Promote to error:  ccc -Werror=attributes -o test main.c
//     -> Unknown attributes become compilation errors, compilation FAILS
//
// This test validates:
// - src/frontend/parser/parse.rs: dispatch_gcc_attribute() fallback arm (line 786+)
// - src/common/error.rs: WarningKind::Attributes
// - src/driver/cli.rs: -Wattributes / -Wno-attributes flag parsing

int printf(const char *fmt, ...);

// Scenario 1: Simple unknown attribute with no arguments.
// The compiler should warn about 'some_nonexistent_attr' being unrecognized
// but still compile the function. The attribute has no effect on codegen.
__attribute__((some_nonexistent_attr))
int compute_sum(int a, int b) {
    return a + b;
}

// Scenario 2: Unknown attribute with a parenthesized string argument.
// The compiler should warn about 'fake_optimize' and skip its arguments
// via skip_balanced_parens(). The function compiles and runs normally.
__attribute__((fake_optimize("fast")))
int compute_product(int a, int b) {
    return a * b;
}

// Scenario 3: Mixed known and unknown attributes in the same list.
// 'noinline' is a recognized attribute (parsed at line 756 of parse.rs)
// and should be applied. 'bogus_attribute' is unknown and should warn.
// This verifies the parser correctly processes each attribute independently.
__attribute__((noinline, bogus_attribute))
int compute_diff(int a, int b) {
    return a - b;
}

int main(void) {
    int sum = compute_sum(10, 20);          // 10 + 20 = 30
    int product = compute_product(3, 7);     // 3 * 7 = 21
    int diff = compute_diff(100, 58);        // 100 - 58 = 42

    // Print results to verify correct compilation and execution
    // despite the presence of unknown attributes.
    // Expected output: "30 21 42"
    printf("%d %d %d\n", sum, product, diff);

    return 0;
}
