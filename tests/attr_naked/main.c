// Test: __attribute__((naked)) function attribute
//
// Compile: ccc -o test main.c
//
// Verifies that:
// 1. __attribute__((naked)) is parsed and accepted on functions
// 2. __attribute__((__naked__)) underscore form is accepted
// 3. Naked functions suppress compiler-generated prologue/epilogue
// 4. The function body must contain only inline assembly
// 5. Naked functions can be called and return values via registers
//
// The naked attribute tells the compiler to not generate prologue code
// (stack frame setup, callee-saved register saves) or epilogue code
// (stack frame teardown, return instruction) for the function. The
// programmer must provide all entry/exit code via inline assembly.
//
// This is commonly used for:
// - Interrupt service routines (ISRs)
// - Context switch trampolines
// - Custom calling conventions
// - Boot code / startup routines
//
// Architecture note: The inline assembly is x86-64 specific. Other
// architectures would need different assembly syntax. The test uses
// expected.skip.* files for non-x86 architectures.

int printf(const char *fmt, ...);

// Scenario 1: naked function returning a constant via register
// The function must manually set the return register (eax on x86-64)
// and execute a ret instruction.
__attribute__((naked))
int get_magic(void) {
    __asm__ volatile (
        "movl $42, %eax\n\t"
        "ret\n\t"
    );
}

// Scenario 2: __naked__ underscore form
// Same behavior, different attribute spelling.
__attribute__((__naked__))
int get_value(void) {
    __asm__ volatile (
        "movl $100, %eax\n\t"
        "ret\n\t"
    );
}

// Scenario 3: naked function that uses its parameter
// On x86-64 SysV ABI, first integer arg is in %edi.
// We add 10 to it and return via %eax.
__attribute__((naked))
int add_ten(int x) {
    __asm__ volatile (
        "leal 10(%edi), %eax\n\t"
        "ret\n\t"
    );
}

int main(void) {
    int a = get_magic();    // Expected: 42
    int b = get_value();    // Expected: 100
    int c = add_ten(15);    // Expected: 25

    printf("%d %d %d\n", a, b, c);
    return 0;
}
