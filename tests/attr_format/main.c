// Test: __attribute__((format(printf, N, M))) function attribute
//
// Verifies that:
// 1. __attribute__((format(printf, N, M))) is parsed and accepted on functions
// 2. __attribute__((__format__(printf, N, M))) underscore form is accepted
// 3. format(printf, 1, 2) works for direct printf-like functions
// 4. format(printf, 2, 3) works for functions with a non-format prefix parameter
// 5. Printf-like wrapper functions with the format attribute compile and execute correctly
// 6. Variadic forwarding with __builtin_va_list works in format-attributed functions
//
// __attribute__((format(printf, N, M))) semantics (GCC extension):
// - Parameter N (1-indexed) is the format string
// - Parameter M (1-indexed) is the first argument to check, or 0 for vprintf-like
// - Compiler checks format specifier/argument type compatibility at call sites
// - Emits warnings for mismatched format specifiers (e.g., %d with char*)

// Forward-declare printf with format attribute on declaration
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *fmt, __builtin_va_list ap);

// Scenario 1: format(printf, 2, 3) - format string is 2nd parameter
// Tests offset parameter index where format string is NOT the 1st param
__attribute__((format(printf, 2, 3)))
int log_with_level(int level, const char *fmt, ...) {
    printf("[%d] ", level);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// Scenario 2: __format__ underscore form, format(printf, 1, 2)
// Tests the GNU alternate spelling of the attribute name
__attribute__((__format__(printf, 1, 2)))
int my_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// Scenario 3: format attribute with custom return value
// Verifies format attribute does not interfere with return values
__attribute__((format(printf, 1, 2)))
int print_and_count(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vprintf(fmt, ap);
    __builtin_va_end(ap);
    return 99;
}

int main(void) {
    // Call printf with format attribute on its declaration
    printf("%d\n", 42);

    // Call log_with_level: format(printf, 2, 3) - offset format index
    log_with_level(1, "%s=%d\n", "answer", 42);

    // Call my_printf: underscore __format__ form
    my_printf("sum: %d\n", 10 + 20);

    // Call print_and_count: format attribute, verify return value
    int r = print_and_count("result: %d\n", 7 * 8);
    printf("ret=%d\n", r);

    return 0;
}
