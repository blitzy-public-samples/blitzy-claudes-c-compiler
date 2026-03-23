// Test: GNU IFUNC dispatch on x86-64
//
// GNU IFUNC (Indirect Function) allows runtime function dispatch via
// resolver functions. The resolver is called once at program startup
// to select which implementation function to use.
//
// This test verifies that CCC's x86-64 linker correctly:
// 1. Recognizes STT_GNU_IFUNC symbol types in the symbol table
// 2. Creates IPLT (Indirect PLT) stubs with IRELATIVE relocations
// 3. Allocates IFUNC GOT slots for IFUNC symbols
// 4. At runtime, the IPLT stub calls the resolver and caches the result
//
// Architecture: x86-64 only (skip arm, riscv, i686)

// Forward declare puts from libc for output
int puts(const char *s);

// The actual implementation function that will be dispatched to.
// Returns 42 as a sentinel value to verify correct dispatch.
static int my_func_impl(void) {
    return 42;
}

// Function pointer type for the resolver return value.
// The resolver must return a pointer to a function with the same
// signature as the IFUNC-declared function.
typedef int (*func_ptr_t)(void);

// Resolver function: called once at startup to select implementation.
// Returns a function pointer to the chosen implementation.
// The CRT startup code invokes this via IRELATIVE relocation processing.
static func_ptr_t my_func_resolve(void) {
    return my_func_impl;
}

// Declare my_func as an IFUNC — this causes the assembler to emit
// an STT_GNU_IFUNC symbol type (value 10, defined in
// src/backend/elf/constants.rs:89 as STT_GNU_IFUNC = 10).
//
// The x86-64 linker flow:
// 1. collect_ifunc_symbols() in plt_got.rs collects STT_GNU_IFUNC symbols
// 2. emit_executable() in emit_exec.rs receives ifunc_symbols
// 3. IPLT entries are created (16 bytes each, jmp *got(%rip) + padding)
// 4. IFUNC GOT slots allocated (8 bytes each, stores resolver addr initially)
// 5. IRELATIVE relocations emitted (24 bytes per RELA entry)
// 6. Resolver addresses saved and symbol addresses redirected to IPLT entries
int my_func(void) __attribute__((ifunc("my_func_resolve")));

int main(void) {
    // Call the IFUNC-dispatched function.
    // At this point the CRT has already processed IRELATIVE relocations,
    // called my_func_resolve(), and stored the result (pointer to
    // my_func_impl) in the IFUNC GOT slot. The IPLT stub loads from
    // the GOT slot and jumps to my_func_impl.
    int result = my_func();

    // Verify the resolver dispatched to the correct implementation
    if (result == 42) {
        puts("IFUNC dispatch OK");
        return 0;
    }

    return 1;
}
