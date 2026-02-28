// Test: tail call optimization
//
// Compile: ccc -O2 -o test main.c
//
// This test verifies that the compiler performs tail call optimization (TCO)
// by using deep recursion that would overflow the stack without it.
//
// The function tail_sum performs 1,000,000 recursive calls. The default
// stack size on Linux is 8 MiB. Each call frame requires ~32-64 bytes,
// so 1 million calls would need ~32-64 MiB of stack, which exceeds the
// limit and causes a segfault WITHOUT tail call optimization.
//
// With TCO at -O2, the compiler transforms the tail-recursive call into
// a loop, using constant stack space regardless of recursion depth.
//
// The function computes: sum(1..1000000) mod 2^32 = 1784293664
// (Using unsigned int to ensure well-defined wrapping behavior.)
//
// Tests:
// - src/backend/x86/codegen/peephole/passes/tail_call.rs (x86-64 existing)
// - src/backend/arm/codegen/peephole.rs (AArch64 new)
// - src/backend/riscv/codegen/peephole.rs (RISC-V new)
// - src/backend/i686/codegen/peephole.rs (i686 new)

int printf(const char *fmt, ...);

// Tail-recursive accumulator function.
// The recursive call is in tail position: the return value of the
// recursive call is immediately returned without further computation.
static unsigned int tail_sum(unsigned int n, unsigned int acc) {
    if (n == 0) return acc;
    return tail_sum(n - 1, acc + n);
}

int main(void) {
    // 1,000,000 recursive calls — would segfault without TCO
    unsigned int result = tail_sum(1000000, 0);
    printf("%u\n", result);
    return 0;
}
