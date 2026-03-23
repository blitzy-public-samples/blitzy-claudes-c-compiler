// Test: binary size comparison payload for optimization tiers -Os and -Oz
//
// This test provides a code payload with many small static functions
// and constant-bound loops, designed to produce measurable binary size
// differences when compiled at different optimization levels:
//
//   -O3: All functions inlined + loop unrolled -> largest binary
//   -O2: Most functions inlined, no loop unrolling -> medium-large
//   -Os: Inlining threshold halved, no loop unrolling -> smaller
//   -Oz: No inlining at all, no loop unrolling -> smallest binary
//
// The test computes a deterministic result (8952) that is identical
// regardless of optimization level, verifying correctness at -Os
// (specified in compile_flags).
//
// Binary size comparison across optimization levels:
// The standard CCC test harness compiles each test directory once (using
// compile_flags) and verifies stdout/exit code. Multi-compilation size
// comparison (verifying size(-Oz) < size(-Os) < size(-O3)) is performed
// externally by the Gate 2 validation infrastructure (V-200 through
// V-207), which compiles this same payload at -O2, -O3, -Os, and -Oz
// and checks the size ordering. This test serves as:
//   1. Correctness verification at -Os (via compile_flags)
//   2. The shared payload for external binary size comparison

int printf(const char *fmt, ...);

// 20 small static functions: each performs simple arithmetic.
// At -O3, all are inlined at every call site (each is well under
// the MAX_INLINE_INSTRUCTIONS threshold of 60 in src/passes/inline.rs).
// At -Os (50% threshold), fewer qualify for inlining.
// At -Oz, NONE are inlined — each remains a separate call.
static int add_one(int x) { return x + 1; }
static int mul_two(int x) { return x * 2; }
static int sub_three(int x) { return x - 3; }
static int add_five(int x) { return x + 5; }
static int mul_three(int x) { return x * 3; }
static int add_seven(int x) { return x + 7; }
static int sub_one(int x) { return x - 1; }
static int mul_four(int x) { return x * 4; }
static int add_ten(int x) { return x + 10; }
static int sub_two(int x) { return x - 2; }
static int add_three(int x) { return x + 3; }
static int mul_five(int x) { return x * 5; }
static int sub_four(int x) { return x - 4; }
static int add_eight(int x) { return x + 8; }
static int mul_six(int x) { return x * 6; }
static int sub_five(int x) { return x - 5; }
static int add_eleven(int x) { return x + 11; }
static int mul_seven(int x) { return x * 7; }
static int sub_six(int x) { return x - 6; }
static int add_thirteen(int x) { return x + 13; }

// Chain functions: each calls 5 primitive functions in sequence.
// These create nested inlining opportunities at -O3 where both the
// chain AND its callees get inlined into main's loop body.
static int compute_chain(int x) {
    x = add_one(x);
    x = mul_two(x);
    x = sub_three(x);
    x = add_five(x);
    x = mul_three(x);
    return x;
}

static int compute_chain2(int x) {
    x = add_seven(x);
    x = sub_one(x);
    x = mul_four(x);
    x = add_ten(x);
    x = sub_two(x);
    return x;
}

static int compute_chain3(int x) {
    x = add_three(x);
    x = mul_five(x);
    x = sub_four(x);
    x = add_eight(x);
    x = mul_six(x);
    return x;
}

static int compute_chain4(int x) {
    x = sub_five(x);
    x = add_eleven(x);
    x = mul_seven(x);
    x = sub_six(x);
    x = add_thirteen(x);
    return x;
}

int main(void) {
    int sum = 0;

    // Constant-bound loop: 16 iterations, calling all 4 chains.
    // At -O3: loop_unroll may unroll this (16 <= 32 iteration limit),
    //         AND all chain/primitive functions get inlined -> large body.
    // At -Os/-Oz: no unrolling, reduced or no inlining -> compact loop.
    for (int i = 0; i < 16; i++) {
        sum += compute_chain(i);
        sum += compute_chain2(i);
        sum += compute_chain3(i);
        sum += compute_chain4(i);
    }

    printf("%d\n", sum);
    return 0;
}
