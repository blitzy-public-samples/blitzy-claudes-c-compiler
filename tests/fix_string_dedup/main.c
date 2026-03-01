/* Regression test for string literal deduplication (-fmerge-constants).
 * GCC deduplicates identical string literals by default, placing them
 * at the same address in .rodata. CCC must do the same.
 * Based on compiler_suite_0011_0092 failure on RISC-V where code
 * comparing pointers to identical string literals failed because
 * CCC emitted separate copies for each occurrence.
 *
 * Expected output: 5
 * Expected return: 0
 */

int printf(const char *fmt, ...);

struct entry {
    const char *name;
    int value;
};

/* Test 1: Basic string literal deduplication.
 * Multiple pointers initialized with the same string literal should
 * point to the same address after deduplication. */
static int test_basic_dedup(void) {
    const char *s1 = "hello world";
    const char *s2 = "hello world";
    const char *s3 = "hello world";
    /* All three should point to same .rodata address */
    return (s1 == s2) && (s2 == s3);
}

/* Test 2: Struct field string deduplication.
 * This mirrors the compiler_suite_0011_0092 failure pattern where
 * struct fields containing pointers to identical string literals
 * were compared via memcmp. With deduplication, the pointer values
 * in the struct fields should be identical. */
static int test_struct_dedup(void) {
    struct entry a;
    struct entry b;
    a.name = "shared_key";
    a.value = 42;
    b.name = "shared_key";
    b.value = 99;
    /* With deduplication, a.name == b.name */
    return a.name == b.name;
}

/* Test 3: Different string literals must NOT be merged.
 * Sanity check that the deduplication is content-based. */
static int test_different_strings(void) {
    const char *s1 = "alpha";
    const char *s2 = "beta";
    return s1 != s2;
}

/* Test 4: Empty string literals should also be deduplicated. */
static int test_empty_dedup(void) {
    const char *s1 = "";
    const char *s2 = "";
    return s1 == s2;
}

/* Test 5: String literals used across function boundaries should
 * also be deduplicated. The same literal in two different functions
 * must resolve to the same .rodata address. */
static const char *get_greeting(void) {
    return "greetings";
}

static int test_cross_function_dedup(void) {
    const char *local = "greetings";
    const char *from_func = get_greeting();
    return local == from_func;
}

int main(void) {
    int passed = 0;

    passed += test_basic_dedup();           /* 1 if dedup works */
    passed += test_struct_dedup();          /* 1 if struct field dedup works */
    passed += test_different_strings();     /* 1 if different strings stay separate */
    passed += test_empty_dedup();           /* 1 if empty strings dedup */
    passed += test_cross_function_dedup();  /* 1 if cross-function dedup works */

    printf("%d\n", passed);                 /* 5 if all pass */

    return 0;
}
