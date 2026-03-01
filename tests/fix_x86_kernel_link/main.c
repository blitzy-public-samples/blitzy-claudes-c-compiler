/* Regression test for x86 standalone kernel link errors.
 * Exercises the x86 assembler parser patterns that caused Linux kernel
 * standalone link failures:
 *
 * 1. Complex data value expressions with additive/subtractive operations
 *    (e.g., symbol + (N << 12)) - parse_data_values was splitting on
 *    the wrong " - " boundary
 * 2. Numeric forward label references (0f) - not resolved during
 *    altinstructions-like macro expansion
 * 3. Nested expression evaluation like alt_max_2/alt_max_3 macro output
 *    where fragments like (((1 were left unevaluated
 *
 * Expected output: 4
 * Expected return: 0
 */

int printf(const char *fmt, ...);

/* Test 1: Numeric forward label references (0f, 1f).
 * The Linux kernel uses numeric labels extensively in altinstructions:
 *   0: original_insn
 *   .quad 0b, 1f
 *   1: replacement_insn
 * The assembler must resolve 0f/1f forward references correctly.
 * Bug: "Undefined reference to '0f'" during altinstructions expansion. */
static int test_forward_labels(void) {
    int result;
    __asm__ volatile(
        "xorl %0, %0\n\t"
        "jmp 0f\n\t"
        "addl $99, %0\n\t"
        "0:\n\t"
        "addl $1, %0\n\t"
        : "=&r"(result)
    );
    return result;  /* 1 */
}

/* Test 2: Numeric backward label references (0b, 1b).
 * Backward numeric label references used in kernel loop constructs
 * and alternative instruction fixup tables. */
static int test_backward_labels(void) {
    int result;
    __asm__ volatile(
        "movl $3, %0\n\t"
        "0:\n\t"
        "decl %0\n\t"
        "jnz 0b\n\t"
        : "=&r"(result)
    );
    return (result == 0) ? 1 : 0;  /* 1 */
}

/* Test 3: Complex additive/subtractive shift expressions.
 * Simulates the kernel pattern: level1_fixmap_pgt + (N << 12)
 * where parse_data_values incorrectly split on " - " inside
 * parenthesized subexpressions. Tests the expression parser with:
 *   (A << shift) - (B << shift) + constant
 * Bug: "parse_data_values fails to parse complex 'symbol + expr - const
 * + const' expressions. The ' - ' check splits on the wrong boundary." */
static int test_complex_expressions(void) {
    int val;
    __asm__ volatile(
        "movl $((5 << 4) - (2 << 4) + 7), %0\n\t"
        : "=r"(val)
    );
    /* (5<<4)=80, (2<<4)=32, 80-32+7=55 */
    return (val == 55) ? 1 : 0;  /* 1 */
}

/* Test 4: Deeply nested parenthesized expression evaluation.
 * Simulates alt_max_2/alt_max_3 macro output where fragments
 * like (((1 were left unevaluated. Tests proper nesting of
 * multiple levels of parentheses with mixed operators.
 * Bug: "Mangled expression '(((1' in .altinstructions: likely
 * an expression fragment from alt_max_2/alt_max_3 macros not
 * being properly evaluated." */
static int test_nested_expressions(void) {
    int val;
    __asm__ volatile(
        "movl $(((1 + 2) * (3 - 1)) + ((4 >> 1) << 2)), %0\n\t"
        : "=r"(val)
    );
    /* (1+2)*(3-1) = 3*2 = 6, (4>>1)<<2 = 2<<2 = 8, 6+8 = 14 */
    return (val == 14) ? 1 : 0;  /* 1 */
}

int main(void) {
    int total = 0;
    total += test_forward_labels();      /* 1 */
    total += test_backward_labels();     /* 1 */
    total += test_complex_expressions(); /* 1 */
    total += test_nested_expressions();  /* 1 */
    printf("%d\n", total);               /* 4 */
    return 0;
}
