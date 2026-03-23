// Test: GNU IFUNC dispatch on AArch64
//
// GNU IFUNC (Indirect Function) allows runtime function dispatch via
// resolver functions. The resolver is called once at program startup
// to select which implementation function to use.
//
// This test verifies that CCC's AArch64 linker correctly:
// 1. Recognizes STT_GNU_IFUNC symbol types in the symbol table
// 2. Creates IPLT (Indirect PLT) stubs with IRELATIVE relocations
// 3. Allocates IPLT GOT slots for IFUNC symbols
// 4. At runtime, the IPLT stub calls the resolver and caches the result
//
// How IFUNC works in CCC's AArch64 backend:
//   - The __attribute__((ifunc("resolver"))) attribute causes the compiler
//     to emit assembly with .type my_func, @gnu_indirect_function and
//     .set my_func, my_func_resolve
//   - The assembler translates this to a symbol with STT_GNU_IFUNC type
//     (value 10, defined in src/backend/elf/constants.rs)
//   - The AArch64 linker (src/backend/arm/linker/emit_static.rs):
//     * Pre-counts IFUNC symbols by filtering for STT_GNU_IFUNC
//     * Collects and sorts IFUNC symbol names deterministically
//     * Allocates IPLT GOT slots (one 8-byte slot per IFUNC)
//     * Builds IPLT stubs (16 bytes each: ADRP + LDR + BR + NOP)
//     * Emits IRELATIVE relocations (R_AARCH64_IRELATIVE = 0x408)
//     * Redirects IFUNC symbols to their PLT stub addresses
//   - At runtime, the CRT startup processes IRELATIVE relocations,
//     calls the resolver function, and stores the result in the GOT slot
//
// Architecture: AArch64 only (skip x86, riscv, i686)
// Execution: via QEMU user-mode emulation

// Forward declare puts from libc for output — no system headers needed.
// CCC's test infrastructure links against libc which provides puts.
int puts(const char *s);

// The actual implementation function that will be dispatched to.
// Returns a known value (42) so main() can verify correct dispatch.
static int my_func_impl(void) {
    return 42;
}

// Function pointer type for the resolver return value.
// The resolver must return a pointer to a function with the same
// signature as the IFUNC-declared function (int (void) in this case).
typedef int (*func_ptr_t)(void);

// Resolver function: called once at startup to select implementation.
// Returns a function pointer to the chosen implementation.
// In a real-world scenario, the resolver might check CPU features
// (e.g., via HWCAP) to select an optimized implementation. Here we
// simply return the single implementation for testing purposes.
static func_ptr_t my_func_resolve(void) {
    return my_func_impl;
}

// Declare my_func as an IFUNC — this causes the assembler to emit
// an STT_GNU_IFUNC symbol type (value 10). The linker creates an
// IPLT stub with an IRELATIVE relocation that calls my_func_resolve
// at program startup to determine the actual function address.
//
// At link time, the AArch64 linker:
//   1. Detects the STT_GNU_IFUNC symbol type (info & 0xf == 10)
//   2. Creates a 16-byte IPLT stub (ADRP + LDR + BR + NOP)
//   3. Allocates an 8-byte IPLT GOT slot
//   4. Emits an R_AARCH64_IRELATIVE relocation with the resolver address
//   5. Redirects the symbol to point to the IPLT stub
//   6. Changes the symbol type from IFUNC to FUNC for normal relocation
int my_func(void) __attribute__((ifunc("my_func_resolve")));

int main(void) {
    // Call the IFUNC-dispatched function.
    // At runtime, this call goes through the IPLT stub which loads
    // the resolved function address from the GOT slot and branches to it.
    int result = my_func();

    // Verify the resolver dispatched to the correct implementation.
    // If IFUNC dispatch worked correctly, my_func_impl was called and
    // returned 42.
    if (result == 42) {
        puts("IFUNC dispatch OK");
        return 0;
    }

    // If we reach here, IFUNC dispatch failed — the resolver did not
    // correctly select my_func_impl, or the IPLT/GOT mechanism is broken.
    return 1;
}
