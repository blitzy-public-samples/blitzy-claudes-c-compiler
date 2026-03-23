// Test: __attribute__((alias("target"))) function attribute
//
// Verifies that:
// 1. __attribute__((alias("target_fn"))) is parsed and accepted
// 2. __attribute__((__alias__("target_fn"))) underscore form is accepted
// 3. Alias symbols are correctly generated in the ELF output
//    (.globl alias_name + .set alias_name, target_name)
// 4. Calling an alias resolves to the target function
// 5. Multiple aliases for the same target function work correctly
//
// __attribute__((alias("target"))) semantics (GCC extension):
// - Creates a symbol that is another name for the target symbol
// - Target and alias must be in the same translation unit
// - Alias declaration has no function body
// - At link time, alias resolves to same address as target
// - Codegen emits: .globl alias_name + .set alias_name, target_name

int printf(const char *fmt, ...);

// Target function 1: addition
int target_add(int a, int b) {
    return a + b;
}

// Scenario 1: Basic alias using __attribute__((alias("target_add")))
int alias_add(int a, int b) __attribute__((alias("target_add")));

// Target function 2: multiplication
int target_mul(int a, int b) {
    return a * b;
}

// Scenario 2: __alias__ underscore form
int alias_mul(int a, int b) __attribute__((__alias__("target_mul")));

// Target function 3: subtraction
int target_sub(int a, int b) {
    return a - b;
}

// Scenario 3: Multiple aliases for the same target
int alias_sub1(int a, int b) __attribute__((alias("target_sub")));
int alias_sub2(int a, int b) __attribute__((alias("target_sub")));

int main(void) {
    // Call original target functions directly
    int r1 = target_add(3, 4);    // 3 + 4 = 7
    int r2 = target_mul(5, 6);    // 5 * 6 = 30
    int r3 = target_sub(10, 3);   // 10 - 3 = 7

    // Call through aliases — must produce identical results
    int r4 = alias_add(3, 4);     // 7 (resolves to target_add)
    int r5 = alias_mul(5, 6);     // 30 (resolves to target_mul)
    int r6 = alias_sub1(10, 3);   // 7 (resolves to target_sub)
    int r7 = alias_sub2(10, 3);   // 7 (resolves to target_sub)

    printf("%d %d %d %d %d %d %d\n", r1, r2, r3, r4, r5, r6, r7);
    return 0;
}
