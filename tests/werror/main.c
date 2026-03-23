// Test: -Werror and -Werror=<name> diagnostic promotion
//
// Compilation scenarios:
//   Default:                    ccc -o test main.c
//     -> -Wreturn-type warning emitted, compilation succeeds, program runs
//   Granular promote+demote:   ccc -Werror=return-type -Wno-error=return-type -o test main.c
//     -> -Werror=return-type promotes, -Wno-error=return-type demotes back; net = warning
//   Granular promote only:     ccc -Werror=return-type -o test main.c
//     -> return-type warning promoted to error, compilation FAILS
//   Blanket promote:           ccc -Werror -o test main.c
//     -> ALL warnings promoted to errors, compilation FAILS
//
// This test verifies the diagnostic engine's warning-to-error promotion
// mechanism in src/common/error.rs: WarningConfig, -Werror, -Werror=<name>,
// -Wno-error=<name>, and their left-to-right processing order.

int printf(const char *fmt, ...);

// This function triggers -Wreturn-type: the compiler's conservative
// fall-through analysis (src/frontend/sema/analysis.rs:260-277) detects
// that control can reach the end of a non-void function without a return
// statement when x == 0.
//
// At RUNTIME, we only call this with non-zero values, so the missing
// return path is never actually taken (no undefined behavior).
int check_value(int x) {
    if (x > 0) {
        return 1;
    }
    if (x < 0) {
        return -1;
    }
    // x == 0: falls through without return -> triggers -Wreturn-type
}

// Clean helper function — no warnings
int multiply(int a, int b) {
    return a * b;
}

int main(void) {
    // Test check_value with non-zero inputs (avoids UB from missing return)
    int pos = check_value(42);    // returns 1
    int neg = check_value(-10);   // returns -1

    // Test clean function
    int product = multiply(pos - neg, 10);  // (1 - (-1)) * 10 = 2 * 10 = 20

    // Print results to verify correct compilation and execution
    // Expected output: "1 -1 20"
    printf("%d %d %d\n", pos, neg, product);

    return 0;
}
