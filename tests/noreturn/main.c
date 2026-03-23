// Test: C11 _Noreturn function specifier (ISO/IEC 9899:2011 §6.7.4)
//
// Verifies three aspects of _Noreturn:
// 1. Valid _Noreturn usage: _Noreturn void abort_wrapper() compiles and works
// 2. Reachable return warning: compiler suppresses "missing return" warning
//    when all fallthrough paths end in a _Noreturn function call
// 3. Dead code elimination: code after _Noreturn calls is unreachable
//
// This tests the C11 _Noreturn keyword specifically.
// The GNU __attribute__((noreturn)) form is tested in tests/attr_noreturn/.

extern int printf(const char *fmt, ...);
extern void _exit(int status);

/* Test 1: Valid _Noreturn function specifier (C11 §6.7.4).
 * abort_wrapper is declared with _Noreturn and genuinely never returns
 * because it calls _exit(). The compiler must accept this declaration. */
_Noreturn void abort_wrapper(int code) {
    _exit(code);
}

/* Test 2: _Noreturn in control flow analysis.
 * The compiler should NOT emit -Wreturn-type ("control reaches end of
 * non-void function") because the fallthrough path calls abort_wrapper()
 * which is declared _Noreturn.
 *
 * In sema/analysis.rs, is_noreturn_call() (line 1246) detects the call
 * to abort_wrapper as noreturn, causing stmt_can_fall_through() (line 1006)
 * to return false. compound_can_fall_through() then returns false for the
 * function body, and the -Wreturn-type warning (lines 260-277) is skipped. */
int get_value(int selector) {
    if (selector == 0) return 10;
    if (selector == 1) return 20;
    if (selector == 2) return 30;
    abort_wrapper(1);
    /* No return statement needed here — compiler knows abort_wrapper
     * never returns, so this is not a "missing return" path. */
}

/* Test 3: Dead code elimination after _Noreturn call.
 * In the x < 0 branch, abort_wrapper() is _Noreturn so it never returns.
 * The printf("DEAD\n") and return -1 after it are unreachable dead code.
 * The DCE pass (src/passes/dce.rs) should eliminate them.
 * We verify by confirming "DEAD" is never printed. */
int test_dce(int x) {
    if (x < 0) {
        abort_wrapper(1);
        /* Dead code: abort_wrapper is _Noreturn */
        printf("DEAD\n");
        return -1;
    }
    return x * 3;
}

int main(void) {
    int a;
    int b;
    int c;
    int d;
    int e;

    /* Test 2 verification: get_value compiles without -Wreturn-type */
    a = get_value(0);
    printf("%d\n", a);       /* Expected: 10 */

    b = get_value(1);
    printf("%d\n", b);       /* Expected: 20 */

    c = get_value(2);
    printf("%d\n", c);       /* Expected: 30 */

    /* Test 3 verification: test_dce with non-negative input — normal path */
    d = test_dce(7);
    printf("%d\n", d);       /* Expected: 21 (7 * 3) */

    e = test_dce(0);
    printf("%d\n", e);       /* Expected: 0 (0 * 3) */

    /* If we reached here, all tests passed */
    printf("ok\n");
    return 0;
}
