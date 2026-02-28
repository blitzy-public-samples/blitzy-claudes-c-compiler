// Test: inline C99 mode linkage
//
// Compile: ccc -o test main.c
//
// CCC defaults to C99/C11 inline semantics (gnu89_inline = false).
// No special flags are needed.
//
// Under C99/C11 inline semantics (ISO C99 §6.7.4p7):
//   - `inline` without `extern` or `static` = inline definition only
//     (no external definition is emitted; lowered as local/static)
//   - If any file-scope declaration lacks `inline` or has `extern`,
//     the definition provides an external definition (global)
//   - `static inline` always provides an internal (static) definition
//
// This is the OPPOSITE of GNU89 inline semantics where:
//   - `inline` without `extern` = external definition
//   - `extern inline` = inline definition only (no external def)
//
// This test verifies three C99 inline scenarios:
//   1. Plain `inline` function (inline-only def, lowered as static)
//   2. `inline` function with `extern` declaration (external definition)
//   3. Address-of inline function (forces materialization)

int printf(const char *fmt, ...);

// Case 1: Plain `inline` function — C99 inline-only definition
//
// Under C99, `inline` without `extern` or `static` is an "inline definition."
// It does NOT provide an external definition. CCC lowers it as a static/local
// function, making it callable within this translation unit. If the compiler
// chooses not to inline it, it emits a local symbol as a fallback.
inline int add_one(int x) {
    return x + 1;
}

// Case 2: `inline` function with `extern` declaration — external definition
//
// The function is defined as `inline`, but there is also a non-inline `extern`
// declaration in this translation unit. Per C99 §6.7.4p7, if any file-scope
// declaration of the function does NOT include `inline`, then the definition
// provides an external definition. The `extern int mul_two(int x);`
// declaration (without `inline`) triggers this rule.
inline int mul_two(int x) {
    return x * 2;
}
extern int mul_two(int x);  // Non-inline declaration forces external definition

// Case 3: Taking address of an inline function
//
// Even though `sub_three` is a C99 inline-only definition, taking its address
// with `&sub_three` forces the compiler to materialize the function body
// (emit a real function with an address). The function is then called through
// a function pointer to verify correct behavior.
inline int sub_three(int x) {
    return x - 3;
}

int main(void) {
    // Test case 1: Call plain inline function
    // add_one is an inline-only definition (C99) — lowered as static
    int a = add_one(10);   // 10 + 1 = 11

    // Test case 2: Call extern-declared inline function
    // mul_two has an extern declaration → provides external definition
    int b = mul_two(10);   // 10 * 2 = 20

    // Test case 3: Call through function pointer to inline function
    // Taking &sub_three forces materialization of the inline-only definition
    int (*fp)(int) = &sub_three;
    int c = fp(10);        // 10 - 3 = 7

    printf("%d %d %d\n", a, b, c);
    return 0;
}
