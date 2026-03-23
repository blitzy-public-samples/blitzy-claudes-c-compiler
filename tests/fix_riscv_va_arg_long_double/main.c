/* Regression test for RISC-V va_arg long double struct alignment bug.
 *
 * When a struct containing long double is passed as a variadic argument
 * on the stack (all GP registers exhausted), va_arg must properly align
 * the stack pointer to a 16-byte boundary before reading the struct.
 * Without this alignment, garbage values are read because long double
 * requires 16-byte alignment on RISC-V LP64D.
 *
 * On RISC-V LP64D:
 *   - GP argument registers: a0-a7 (8 registers)
 *   - In variadic functions, ALL variadic arguments use GP regs (not FP)
 *   - long double is 128-bit (16 bytes, quad precision, f128)
 *   - A struct containing long double inherits 16-byte alignment
 *   - Structs up to 2*XLEN (16 bytes) are passed in 2 GP registers
 *   - If not enough GP registers remain, the struct goes on the stack
 *
 * The bug is in src/backend/riscv/codegen/variadic.rs: emit_va_arg_impl
 * has alignment code for direct is_long_double() types (lines 23-32) but
 * does NOT apply 16-byte alignment for struct types that contain long
 * double fields. When a struct is at a non-16-byte-aligned stack offset,
 * va_arg reads shifted bytes producing garbage for the long double value.
 *
 * Bug: current_tasks/fix_riscv_va_arg_long_double_struct.txt
 * Reproducer: compiler_suite_0172_0039 (test_misalign_r10)
 *
 * Expected output:
 *   70
 *   136
 * Expected return: 0
 */

#include <stdarg.h>

int printf(const char *fmt, ...);

/* Struct wrapping a long double.
 * This forces the va_arg codegen path to handle a struct type (not just
 * a bare long double), which is where the bug manifests since the
 * alignment check in emit_va_arg_impl only triggers for
 * result_ty.is_long_double() -- NOT for struct types.
 *
 * Size: 16 bytes on RISC-V LP64D
 * Alignment: 16 bytes (inherited from long double member)
 */
struct ld_wrapper {
    long double value;
};

/* Variadic function that sums count integer arguments, then reads one
 * struct ld_wrapper from the va_list, converts its long double member
 * to int via a double intermediate cast, and returns the total sum.
 *
 * The (double) intermediate cast is necessary because CCC uses
 * __trunctfdf2 for long double -> double conversion, which is the
 * supported path for f128 on RISC-V.
 */
static int test_va_arg_struct(int count, ...) {
    va_list ap;
    int sum = 0;
    int i;
    struct ld_wrapper s;
    double dval;

    va_start(ap, count);
    for (i = 0; i < count; i++) {
        sum += va_arg(ap, int);
    }
    s = va_arg(ap, struct ld_wrapper);
    va_end(ap);

    dval = (double)s.value;
    return sum + (int)dval;
}

int main(void) {
    struct ld_wrapper s;
    int result;

    /* Test 1: struct at start of stack vararg area (aligned).
     *
     * count=7 goes in a0.
     * 7 ints (1-7) go in a1-a7, exhausting all GP registers.
     * struct ld_wrapper goes entirely on the stack at the beginning
     * of the stack vararg area, which is naturally 16-byte aligned.
     *
     * Sum: 1+2+3+4+5+6+7 = 28
     * Struct value: (int)(double)42.0L = 42
     * Result: 28 + 42 = 70
     */
    s.value = 42.0L;
    result = test_va_arg_struct(7, 1, 2, 3, 4, 5, 6, 7, s);
    printf("%d\n", result);

    /* Test 2: struct after one int on stack (MISALIGNED - triggers the bug).
     *
     * count=8 goes in a0.
     * 7 ints (1-7) go in a1-a7, exhausting all GP registers.
     * 8th int (8) goes on the stack (8 bytes).
     * struct ld_wrapper goes on the stack AFTER the 8th int.
     *
     * The struct is now at a stack offset that is 8-byte aligned but
     * NOT 16-byte aligned. Without the alignment fix, va_arg reads
     * from offset 8 instead of aligning to offset 16, causing garbage
     * to be read for the long double value.
     *
     * Sum: 1+2+3+4+5+6+7+8 = 36
     * Struct value: (int)(double)100.0L = 100
     * Result: 36 + 100 = 136
     */
    s.value = 100.0L;
    result = test_va_arg_struct(8, 1, 2, 3, 4, 5, 6, 7, 8, s);
    printf("%d\n", result);

    return 0;
}
