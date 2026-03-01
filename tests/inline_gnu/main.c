// Test: extern inline GNU89 mode linkage
//
// Compile: ccc -fgnu89-inline -o test main.c
//
// This test activates GNU89 inline semantics via the -fgnu89-inline flag.
// Under GNU89 inline rules (the traditional GCC behavior before C99):
//
//   extern inline = inline definition only (no external def emitted)
//                   The function body is available for inlining but the
//                   compiler does NOT emit a standalone global symbol.
//                   If inlining is not possible (e.g., address taken),
//                   the linker expects a definition from another TU.
//
//   inline (no extern) = provides an external definition (global symbol)
//                         The function is emitted as a global, just like
//                         a regular function, but with inlining hints.
//                         Address-of operations work because a real
//                         symbol exists.
//
// This is the OPPOSITE of C99/C11 inline semantics where:
//   extern inline = provides external definition (global)
//   inline (no extern) = inline definition only (no external def)
//
// This test verifies three GNU89 inline scenarios:
//   1. extern inline function — inline-only def, lowered as static,
//      callable via direct call within the same TU
//   2. Plain inline function — external definition, global symbol,
//      callable normally
//   3. Plain inline function with address taken — external definition
//      means the function has a real address and can be called through
//      a function pointer (unlike extern inline which has no address)

int printf(const char *fmt, ...);

// Case 1: extern inline function — GNU89 inline-only definition
//
// Under GNU89, `extern inline` provides an inline definition only.
// The compiler does NOT emit a global symbol for this function.
// CCC's is_gnu_inline_no_extern_def() returns true because:
//   attrs.is_inline() && attrs.is_extern() && self.gnu89_inline
// The function is lowered as static (internal linkage).
// It is still callable within this translation unit — the compiler
// emits a local copy as a fallback if inlining is not performed.
//
// NOTE: In GNU89 mode, taking the address of an extern inline
// function would result in an undefined symbol error because no
// external definition is provided. This matches GCC behavior.
extern inline int add_five(int x) {
    return x + 5;
}

// Case 2: Plain inline function — GNU89 external definition
//
// Under GNU89, `inline` without `extern` provides an external
// definition. The function is emitted as a global symbol, just
// like a regular non-inline function, with inlining hints.
// This is the OPPOSITE of C99 where plain `inline` would be
// inline-only. CCC's is_gnu_inline_no_extern_def() returns false
// (attrs.is_extern() is false), and is_c99_inline_def is also
// false (gnu89_inline is true), so the function is global.
inline int mul_three(int x) {
    return x * 3;
}

// Case 3: Plain inline function with address-of — GNU89 external definition
//
// Under GNU89, plain `inline` (no `extern`) provides an external
// definition (global symbol), so the function has a real address.
// This means taking &sub_two works correctly — the function pointer
// resolves to the global symbol. This contrasts with `extern inline`
// which would NOT have a resolvable address in GNU89 mode.
//
// This case demonstrates a key GNU89 vs C99 difference:
//   GNU89: plain `inline` = external def → address IS available
//   C99:   plain `inline` = inline-only  → address may NOT be available
inline int sub_two(int x) {
    return x - 2;
}

int main(void) {
    // Test case 1: Call extern inline function directly
    // add_five is an inline-only definition (GNU89) — lowered as static
    // Direct calls work because the compiler emits a local fallback body
    int a = add_five(10);   // 10 + 5 = 15

    // Test case 2: Call plain inline function directly
    // mul_three provides an external definition (GNU89) — global symbol
    int b = mul_three(10);  // 10 * 3 = 30

    // Test case 3: Call plain inline function through function pointer
    // sub_two provides an external definition (GNU89) — has a real address
    // Taking &sub_two works because plain inline in GNU89 mode emits a
    // global symbol. This would NOT work with extern inline in GNU89 mode.
    int (*fp)(int) = &sub_two;
    int c = fp(10);         // 10 - 2 = 8

    printf("%d %d %d\n", a, b, c);
    return 0;
}
