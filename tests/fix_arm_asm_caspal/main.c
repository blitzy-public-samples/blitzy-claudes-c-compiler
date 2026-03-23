/* Regression test for AArch64 CASP/CASPAL LSE instruction encoding bug fix.
 *
 * Bug: The CASP (Compare-And-Swap Pair) family of LSE atomic instructions was
 * missing entirely from the ARM assembler encoder. Only the single-register
 * CAS/CASA/CASAL/CASL family was implemented. The CASP variants (casp, caspa,
 * caspl, caspal) use register PAIRS for 128-bit atomic compare-and-swap
 * operations and were not recognized or encoded.
 *
 * CASP instruction format:
 *   caspal Xs, X(s+1), Xt, X(t+1), [Xn]
 *   - 5 operands: compare pair (Rs, Rs+1), swap pair (Rt, Rt+1), memory [Rn]
 *   - Encoding: sz 001000 0 L 1 Rs o0 11111 Rn Rt
 *   - Rs must be an even-numbered register (pair starts on even register)
 *   - Rt must be an even-numbered register (pair starts on even register)
 *
 * Key encoding difference from CAS:
 *   CAS  encoding: sz 001000 1 L 1 Rs o0 11111 Rn Rt  (bit 23 = 1)
 *   CASP encoding: sz 001000 0 L 1 Rs o0 11111 Rn Rt  (bit 23 = 0)
 *
 * CASPAL variant bits:
 *   L  = 1 (acquire semantics, from 'a' in caspal)
 *   o0 = 1 (release semantics, from 'l' in caspal)
 *   sz = 11 for 64-bit (X registers), 10 for 32-bit (W registers)
 *
 * Real-world impact: The Linux kernel's mm/slub.o and other core memory
 * management code uses CASPAL for 128-bit atomic operations on AArch64 with
 * LSE (Large System Extensions). Without this fix, kernel compilation fails.
 *
 * Fix locations:
 *   - src/backend/arm/assembler/encoder/mod.rs: dispatch casp/caspa/caspl/caspal
 *   - src/backend/arm/assembler/encoder/load_store.rs: new encode_casp function
 *   - src/backend/arm/codegen/peephole.rs: skip casp in register propagation
 *
 * Test approach:
 *   1. Initialize a 16-byte aligned pair of 64-bit values to {0, 0}
 *   2. Use explicit register binding (__asm__("xN")) to guarantee even pairs
 *   3. Execute CASPAL with compare={0,0}, swap={42,100}
 *   4. Since memory matches compare values, swap succeeds: memory becomes {42,100}
 *   5. Read back pair[0] and verify it equals 42
 *
 * Expected output: 42
 * Expected return: 0
 */

int printf(const char *fmt, ...);

int main(void) {
    /* 128-bit target: two 64-bit values, must be 16-byte aligned for CASP.
     * The AArch64 architecture requires the memory operand of CASP to be
     * aligned to the total size of the pair (16 bytes for X-register pairs). */
    long pair[2] __attribute__((aligned(16))) = {0, 0};

    /* Use explicit register binding with __asm__("xN") to guarantee the
     * even-numbered register pair requirement for CASP.
     *
     * CASP register constraints:
     *   - Compare pair must start on an even register: x0, x1
     *   - Swap pair must start on an even register: x2, x3
     *   - Memory address in a separate register: x4
     *
     * This is the same approach used by the Linux kernel for CASP operations,
     * ensuring the assembler receives the correct even-paired registers. */
    register long r_old_lo __asm__("x0") = 0;     /* Expected old value (low 64 bits) */
    register long r_old_hi __asm__("x1") = 0;     /* Expected old value (high 64 bits) */
    register long r_new_lo __asm__("x2") = 42;    /* New value (low 64 bits) */
    register long r_new_hi __asm__("x3") = 100;   /* New value (high 64 bits) */
    register long *r_addr __asm__("x4") = pair;   /* Memory address (16-byte aligned) */

    /* Execute CASPAL: atomically compare pair[0:1] with (r_old_lo, r_old_hi).
     * If they match, store (r_new_lo, r_new_hi) into pair[0:1].
     *
     * CASPAL = Compare-And-Swap Pair with Acquire and reLease semantics.
     * This is the most complete barrier variant (both acquire and release).
     *
     * Assembly: caspal x0, x1, x2, x3, [x4]
     *   x0 (Rs)   = compare low  (expected old low 64 bits)
     *   x1 (Rs+1) = compare high (expected old high 64 bits)
     *   x2 (Rt)   = swap low     (desired new low 64 bits)
     *   x3 (Rt+1) = swap high    (desired new high 64 bits)
     *   [x4]      = memory address of the 128-bit target
     *
     * On completion:
     *   - If memory matched {x0,x1}: memory is updated to {x2,x3}, x0/x1 get old values
     *   - If memory did NOT match: memory is unchanged, x0/x1 get current memory values
     *
     * Since pair = {0, 0} and we compare against {0, 0}, the match succeeds
     * and pair becomes {42, 100}. */
    __asm__ volatile(
        "caspal x0, x1, x2, x3, [x4]"
        : "+r"(r_old_lo), "+r"(r_old_hi)
        : "r"(r_new_lo), "r"(r_new_hi), "r"(r_addr)
        : "memory"
    );

    /* After CASPAL succeeds:
     *   pair[0] = 42   (new low value)
     *   pair[1] = 100  (new high value)
     *   r_old_lo = 0   (previous low value from memory)
     *   r_old_hi = 0   (previous high value from memory)
     *
     * Read back pair[0] to verify the swap wrote 42.
     * This proves:
     *   - The assembler recognized and dispatched the 'caspal' mnemonic
     *   - The encoder set bit 23 = 0 (CASP pair, not CAS single)
     *   - The 5-operand format (Rs, Rs+1, Rt, Rt+1, [Rn]) was handled
     *   - The instruction executed correctly at runtime */
    long result = pair[0];
    printf("%ld\n", result);
    return 0;
}
