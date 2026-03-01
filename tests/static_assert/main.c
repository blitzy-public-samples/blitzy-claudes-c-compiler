// Test: C11 _Static_assert declaration (ISO/IEC 9899:2011 §6.7.10)
//
// Verifies seven aspects of _Static_assert:
// 1. File-scope _Static_assert with two-argument form (expression, "message")
// 2. C23 single-argument form without message string
// 3. static_assert keyword alias (C23, recognized by CCC lexer token.rs:433)
// 4. Enum constant expressions in static assertions
// 5. _Static_assert inside struct definition (C11 §6.7.2.1)
// 6. Block-scope _Static_assert inside function body
// 7. String literal concatenation in assertion message
//
// Compile-fail testing limitation: the CCC integration test harness
// convention (main.c + expected.stdout + expected.ret) requires successful
// compilation and execution. Explicit compile-error cases such as
// _Static_assert(0, "should fail") cannot be tested within this harness
// because the harness expects compilation to succeed. The compiler's
// error path (declarations.rs:1261-1268, emitting "static assertion
// failed") is implicitly validated here: if _Static_assert evaluation
// were broken (false positives), these assertions would fail compilation.
// Each assertion expression MUST evaluate to non-zero at compile time
// via eval_const_int_expr_with_enums() in declarations.rs:1260.
//
// Implementation path:
//   Lexer: token.rs:433 — "_Static_assert" | "static_assert" => StaticAssert
//   Parser file scope: declarations.rs:89-93
//   Parser block scope: statements.rs:75-76
//   Parser struct scope: types.rs:616-619
//   Core: declarations.rs:1216-1279 — parse_static_assert()

extern int printf(const char *fmt, ...);

// =================================================================
// Test 1: File-scope _Static_assert with two-argument form (C11 standard)
// These use the classic _Static_assert(expr, "message") syntax.
// Each expression must evaluate to non-zero via const eval at parse time.
// =================================================================
_Static_assert(sizeof(int) >= 4, "int must be at least 4 bytes");
_Static_assert(sizeof(char) == 1, "char must be exactly 1 byte");
_Static_assert(sizeof(short) >= 2, "short must be at least 2 bytes");
_Static_assert(sizeof(int) >= sizeof(short), "int must be >= short");

// =================================================================
// Test 2: C23 single-argument form (no message string)
// declarations.rs:1237-1239 — when no comma follows, message is None.
// =================================================================
_Static_assert(sizeof(int) >= sizeof(short));
_Static_assert(1 + 1 == 2);
_Static_assert(100 > 50);
_Static_assert(sizeof(char) == 1);

// =================================================================
// Test 3: static_assert keyword alias (C23 convenience)
// token.rs:433 maps "static_assert" to TokenKind::StaticAssert
// =================================================================
static_assert(sizeof(int) == 4, "static_assert alias works");
static_assert(1);

// =================================================================
// Test 4: Enum constant expressions in static assertions
// The parser passes enums to eval_const_int_expr_with_enums() at
// declarations.rs:1260 which resolves enum constant values.
// =================================================================
enum { MAGIC = 42, DOUBLE_MAGIC = 84 };
_Static_assert(MAGIC == 42, "enum constant value check");
_Static_assert(DOUBLE_MAGIC == MAGIC * 2, "enum arithmetic check");
_Static_assert(MAGIC > 0, "enum positivity check");

// =================================================================
// Test 5: _Static_assert inside struct definition (C11 §6.7.2.1)
// types.rs:616-619 detects StaticAssert token in struct member list
// =================================================================
struct checked_pair {
    int first;
    _Static_assert(sizeof(int) == 4, "int size check inside struct");
    int second;
};

// =================================================================
// Test 7: String literal concatenation in assertion message
// declarations.rs:1228-1235 concatenates adjacent StringLiteral tokens.
// This follows C standard translation phase 6 string concatenation.
// The kernel uses patterns like: "min" "(" "x" ", " "y" ") error"
// =================================================================
_Static_assert(1, "all" " is" " fine");

int main(void) {
    // =============================================================
    // Test 6: Block-scope _Static_assert inside function body
    // statements.rs:75-76 detects StaticAssert in compound statement
    // =============================================================
    _Static_assert(sizeof(struct checked_pair) == 8, "struct size check in block");

    // C23 single-argument form in block scope
    _Static_assert(sizeof(char) == 1);
    static_assert(2 + 2 == 4);

    // Verify the values used in assertions are correct at runtime too.
    // This provides runtime confirmation that the compile-time assertions
    // evaluated the correct constant expression values.
    printf("%d\n", (int)sizeof(int));                  /* Expected: 4 */
    printf("%d\n", (int)sizeof(char));                 /* Expected: 1 */
    printf("%d\n", (int)sizeof(short));                /* Expected: 2 */
    printf("%d\n", (int)sizeof(struct checked_pair));  /* Expected: 8 */
    printf("%d\n", MAGIC);                             /* Expected: 42 */
    printf("%d\n", DOUBLE_MAGIC);                      /* Expected: 84 */
    printf("ok\n");
    return 0;
}
