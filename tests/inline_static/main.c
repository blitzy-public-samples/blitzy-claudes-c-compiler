// Test: static inline linkage semantics
//
// Compile: ccc -o test main.c
//
// `static inline` functions have internal linkage in all C standard modes
// (C99, C11, GNU89). They are:
//   - NOT exported as global symbols (internal/static linkage)
//   - Available for inlining by the compiler
//   - Always available for address-of operations (&func)
//   - Callable within the translation unit
//
// Unlike C99 plain `inline` or GNU89 `extern inline`, the behavior of
// `static inline` does NOT vary between C99 and GNU89 modes.
//
// CCC implementation:
//   - func.attrs.is_static() == true -> is_static = true (func_lowering.rs:542)
//   - func.attrs.is_inline() == true -> is_inline = true (func_lowering.rs:555)
//   - Skipped when unreferenced (ref_collection.rs:42)
//   - Emitted as STB_LOCAL symbol when referenced
//
// This test verifies:
//   1. Direct call to static inline function
//   2. Address-of static inline function (call via function pointer)
//   3. Nested static inline function calls

int printf(const char *fmt, ...);

// Case 1: Basic static inline function -- direct call
//
// double_val has internal linkage (is_static=true) and is inlinable
// (is_inline=true). The compiler may inline it at the call site or
// emit a local symbol. Either way, the result must be correct.
static inline int double_val(int x) {
    return x * 2;
}

// Case 2: Static inline function whose address is taken
//
// add_ten has internal linkage. Taking its address with &add_ten forces
// the compiler to materialize a real function body with a real address,
// even though the function is eligible for inlining. The address-of
// operation creates a reference that prevents the function from being
// skipped during dead static elimination (ref_collection.rs:42).
static inline int add_ten(int x) {
    return x + 10;
}

// Case 3: Static inline functions calling each other
//
// Both square and square_plus_one are static inline. square_plus_one
// calls square -- this tests that the compiler correctly handles calls
// between static inline functions. The compiler may inline both levels,
// inline only one, or emit both as local symbols.
static inline int square(int x) {
    return x * x;
}

static inline int square_plus_one(int x) {
    return square(x) + 1;
}

int main(void) {
    // Case 1: Direct call to static inline function
    // double_val(5) = 5 * 2 = 10
    int a = double_val(5);

    // Case 2: Address-of static inline function, called via function pointer
    // Taking &add_ten forces the compiler to materialize the function body.
    // The function pointer call must produce the same result as a direct call.
    // fp(15) = add_ten(15) = 15 + 10 = 25
    int (*fp)(int) = &add_ten;
    int b = fp(15);

    // Case 3: Nested static inline calls
    // square_plus_one(4) = square(4) + 1 = 4*4 + 1 = 16 + 1 = 17
    int c = square_plus_one(4);

    printf("%d %d %d\n", a, b, c);
    return 0;
}
