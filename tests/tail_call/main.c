// Test: tail call optimization — self-recursive factorial with large N
//
// Verifies that tail call optimization converts recursive calls to branch
// loops, preventing stack overflow. Without TCO, 2000000 recursive calls
// would require ~96MB of stack (far exceeding the 8MB default), causing
// a segmentation fault.
//
// Tests across all 4 architectures: x86-64, AArch64, RISC-V 64, i686.

int printf(const char *fmt, ...);

// Tail-recursive factorial modulo a large prime.
// The recursive call is in tail position: its return value is directly
// returned without further computation — enabling tail call optimization.
//
// Uses modular arithmetic (mod 1000000007) to prevent integer overflow
// while maintaining a verifiable result: 2000000! mod 1000000007 = 578095319
static long long factorial_mod(int n, long long acc) {
    if (n <= 1)
        return acc;
    return factorial_mod(n - 1, (acc * (long long)n) % 1000000007LL);
}

int main(void) {
    // N = 2000000: requires tail call optimization to avoid stack overflow.
    // Without TCO: ~48 bytes/frame * 2000000 = ~96MB > 8MB stack limit.
    // With TCO: recursive call becomes a jump, using only one stack frame.
    long long result = factorial_mod(2000000, 1);
    printf("%lld\n", result);
    return 0;
}
