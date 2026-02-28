// Test: IFUNC unsupported diagnostic on i686 and RISC-V
//
// GNU IFUNC (Indirect Function) allows runtime function dispatch via
// resolver functions. This feature is supported on x86-64 and AArch64 only.
// On i686 and RISC-V, the linker should emit an unsupported diagnostic
// rather than silently ignoring the IFUNC attribute.
//
// The __attribute__((ifunc("resolver"))) attribute causes the compiler to
// emit the function symbol with STT_GNU_IFUNC type (value 10). The linker
// must then create IRELATIVE relocations to call the resolver at program
// startup. On architectures that do not support IFUNC (i686 and RISC-V),
// the linker must detect STT_GNU_IFUNC symbols and emit an error diagnostic.
//
// Compile: ccc -o test main.c
// Expected on i686/RISC-V: Linker emits unsupported IFUNC diagnostic, exits non-zero
// Expected on x86-64/AArch64: Skipped (IFUNC is supported on these architectures)
// Skip: x86-64 (expected.skip.x86), AArch64 (expected.skip.arm)

// Implementation function that would be dispatched to at runtime.
// Returns a known value (42) so that main() can verify correct dispatch
// on architectures where IFUNC is supported.
static int my_func_impl(void) {
    return 42;
}

// Function pointer type matching the signature of my_func_impl.
// The resolver must return this type so the dynamic linker knows how
// to invoke the resolved implementation.
typedef int (*func_ptr_t)(void);

// Resolver function: called once at program startup to select which
// implementation to use. Returns a function pointer to my_func_impl.
// On architectures with IFUNC support, the runtime linker invokes this
// resolver via IRELATIVE relocations and caches the result in the GOT.
static func_ptr_t my_func_resolve(void) {
    return my_func_impl;
}

// Declare my_func as an IFUNC — this causes the assembler to emit the
// symbol with STT_GNU_IFUNC type (value 10, defined in
// src/backend/elf/constants.rs:89). The linker must handle this via
// IRELATIVE relocations on supported architectures, or emit an
// unsupported diagnostic on i686 and RISC-V.
int my_func(void) __attribute__((ifunc("my_func_resolve")));

int main(void) {
    // If IFUNC dispatch works, my_func() calls my_func_impl() which
    // returns 42, so main returns 0 (success).
    // On unsupported architectures (i686, RISC-V), we never reach here
    // because the linker fails with an unsupported IFUNC diagnostic
    // before producing a binary.
    return my_func() - 42;
}
