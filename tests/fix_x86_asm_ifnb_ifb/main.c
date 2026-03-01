/* Regression test for x86 .ifnb/.ifb conditional assembly directives.
 * Based on IBRS_ENTER macro failure in Linux kernel entry code
 * (arch/x86/entry/entry_64.S) where calling the macro without the
 * optional save_reg argument caused "mov requires 2 operands, got 1"
 * because .ifnb was not recognized by the x86 assembler parser.
 *
 * The fix adds .ifnb and .ifb handling to expand_gas_macros_with_state()
 * in src/backend/x86/assembler/parser.rs following the same pattern
 * as .ifc, and updates is_if_start() to recognize them.
 *
 * Expected output: 4
 * Expected return: 0
 */

int printf(const char *fmt, ...);

/* Test 1: .ifnb with blank parameter (IBRS_ENTER pattern).
 * Defines a macro with an optional save_reg parameter. When called
 * without the argument, \save_reg is blank, so .ifnb should evaluate
 * to false and skip the body. Without the fix, the assembler does NOT
 * recognize .ifnb, processes the body unconditionally, and
 * "movl %reg, \save_reg" expands to "movl %reg, " (missing operand)
 * causing an assembler error. This is the PRIMARY regression test. */
static int test_ifnb_blank(void) {
    int result;
    __asm__ volatile(
        ".macro IBRS_TEST save_reg\n\t"
        "movl $42, %0\n\t"
        ".ifnb \\save_reg\n\t"
        "movl %0, \\save_reg\n\t"
        ".endif\n\t"
        ".endm\n\t"
        "IBRS_TEST\n\t"
        ".purgem IBRS_TEST\n\t"
        : "=r"(result)
    );
    return (result == 42) ? 1 : 0;
}

/* Test 2: .ifnb with non-blank parameter.
 * When the macro is called WITH an argument, \opt is non-blank,
 * so .ifnb should evaluate to true and include the body.
 * This verifies the positive case of .ifnb. */
static int test_ifnb_nonblank(void) {
    int result;
    __asm__ volatile(
        ".macro ADD_IF_PRESENT opt\n\t"
        "movl $10, %0\n\t"
        ".ifnb \\opt\n\t"
        "addl $20, %0\n\t"
        ".endif\n\t"
        ".endm\n\t"
        "ADD_IF_PRESENT yes\n\t"
        ".purgem ADD_IF_PRESENT\n\t"
        : "=r"(result)
    );
    return (result == 30) ? 1 : 0;
}

/* Test 3: .ifb with blank parameter.
 * .ifb is the inverse of .ifnb — the body executes when the parameter
 * IS blank. This is used in kernel macros to provide default behavior
 * when an optional argument is omitted. */
static int test_ifb_blank(void) {
    int result;
    __asm__ volatile(
        ".macro DEFAULT_VAL opt\n\t"
        "movl $0, %0\n\t"
        ".ifb \\opt\n\t"
        "movl $77, %0\n\t"
        ".endif\n\t"
        ".endm\n\t"
        "DEFAULT_VAL\n\t"
        ".purgem DEFAULT_VAL\n\t"
        : "=r"(result)
    );
    return (result == 77) ? 1 : 0;
}

/* Test 4: .ifb with non-blank parameter.
 * When the parameter is non-blank, .ifb should evaluate to false
 * and skip the body. This verifies the negative case of .ifb. */
static int test_ifb_nonblank(void) {
    int result;
    __asm__ volatile(
        ".macro SKIP_DEFAULT opt\n\t"
        "movl $99, %0\n\t"
        ".ifb \\opt\n\t"
        "movl $0, %0\n\t"
        ".endif\n\t"
        ".endm\n\t"
        "SKIP_DEFAULT present\n\t"
        ".purgem SKIP_DEFAULT\n\t"
        : "=r"(result)
    );
    return (result == 99) ? 1 : 0;
}

int main(void) {
    int total = 0;
    total += test_ifnb_blank();     /* 1: .ifnb skips blank param */
    total += test_ifnb_nonblank();  /* 1: .ifnb includes non-blank param */
    total += test_ifb_blank();      /* 1: .ifb includes blank param */
    total += test_ifb_nonblank();   /* 1: .ifb skips non-blank param */
    printf("%d\n", total);          /* 4 */
    return 0;
}
