/* Regression test for macro parameter prefix-match substitution bug.
 *
 * Bug: When a .macro has parameters like `orig` and `orig_len`, the
 * naive String::replace in declaration order causes \orig to match
 * inside \orig_len, producing corrupted output like 42_len instead
 * of the correct value of orig_len.
 *
 * This caused undefined references to `140b_len` and `143f_len` in
 * the Linux kernel's .altinstructions section.
 *
 * Fix: Sort macro parameters by name length (longest first) before
 * substitution, and use boundary-aware replacement that checks the
 * character after a match is not an identifier continuation.
 *
 * Files fixed:
 *   src/backend/asm_preprocess.rs (expand_macros, expand_macros_with)
 *   src/backend/x86/assembler/parser.rs (expand_gas_macros_with_state)
 *
 * Expected output: 3
 * Expected return: 0
 */

int printf(const char *fmt, ...);

/* Test 1: Primary regression test - prefix-overlapping parameters.
 * Defines a macro with parameters `orig` and `orig_len` where `orig`
 * is a prefix of `orig_len`. The macro body uses both \orig and
 * \orig_len. Without the fix, \orig matches inside \orig_len,
 * corrupting it (e.g., \orig_len becomes 42_len). With the fix,
 * longest-match-first substitution and boundary-aware replacement
 * ensure each parameter is independently and correctly substituted. */
static int test_prefix_params(void) {
    int val_orig, val_orig_len;
    __asm__ volatile(
        ".macro PREFIX_TEST orig, orig_len\n\t"
        "movl $\\orig, %0\n\t"
        "movl $\\orig_len, %1\n\t"
        ".endm\n\t"
        "PREFIX_TEST 42, 99\n\t"
        ".purgem PREFIX_TEST\n\t"
        : "=r"(val_orig), "=r"(val_orig_len)
    );
    return (val_orig == 42 && val_orig_len == 99) ? 1 : 0;
}

/* Test 2: Triple prefix overlap — three parameters where each is a
 * prefix of the next: `a`, `ab`, `abc`. This is a more extreme case
 * of the prefix problem. Without longest-match-first, \a would
 * corrupt both \ab (becoming e.g. 10b) and \abc (becoming 10bc). */
static int test_triple_prefix(void) {
    int val_a, val_ab, val_abc;
    __asm__ volatile(
        ".macro TRIPLE_TEST a, ab, abc\n\t"
        "movl $\\a, %0\n\t"
        "movl $\\ab, %1\n\t"
        "movl $\\abc, %2\n\t"
        ".endm\n\t"
        "TRIPLE_TEST 10, 20, 30\n\t"
        ".purgem TRIPLE_TEST\n\t"
        : "=r"(val_a), "=r"(val_ab), "=r"(val_abc)
    );
    return (val_a == 10 && val_ab == 20 && val_abc == 30) ? 1 : 0;
}

/* Test 3: Mixed parameter usage order in body.
 * Uses \orig_len BEFORE \orig in the macro body, verifying that
 * the order of use in the body does not affect substitution
 * correctness. The longer parameter \orig_len is substituted first
 * regardless of body position, and \orig is substituted second
 * without corrupting the already-substituted \orig_len value. */
static int test_mixed_use(void) {
    int result;
    __asm__ volatile(
        ".macro MIXED_TEST orig, orig_len\n\t"
        "movl $\\orig_len, %0\n\t"
        "addl $\\orig, %0\n\t"
        ".endm\n\t"
        "MIXED_TEST 5, 100\n\t"
        ".purgem MIXED_TEST\n\t"
        : "=r"(result)
    );
    /* orig=5, orig_len=100, so result = 100 + 5 = 105 */
    return (result == 105) ? 1 : 0;
}

int main(void) {
    int total = 0;
    total += test_prefix_params();   /* 1: orig vs orig_len correctly separated */
    total += test_triple_prefix();   /* 1: a vs ab vs abc correctly separated */
    total += test_mixed_use();       /* 1: mixed order in body works correctly */
    printf("%d\n", total);           /* 3 */
    return 0;
}
