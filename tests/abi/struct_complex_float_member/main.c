/*
 * ABI integration test: struct layout with _Complex float and _Complex double members
 *
 * Verifies that CCC correctly implements struct layout, alignment, and
 * value-roundtrip through the calling convention for structs containing
 * _Complex float and _Complex double members, across all four target
 * architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * Type information (from src/common/types.rs):
 *
 *   _Complex float (CType::ComplexFloat):
 *     - Size:  8 bytes on ALL architectures (2 x sizeof(float))
 *     - ABI alignment: 4 bytes on ALL architectures
 *     - Preferred alignment (__alignof__): 4 on ALL architectures
 *     - Layout: real part at byte 0, imaginary part at byte 4
 *
 *   _Complex double (CType::ComplexDouble):
 *     - Size: 16 bytes on ALL architectures (2 x sizeof(double))
 *     - ABI alignment: 4 on i686, 8 on LP64
 *     - Preferred alignment (__alignof__): 8 on ALL architectures
 *       (i686 preferred override in preferred_align_ctx, types.rs line 1380
 *        and preferred_alignof_type_spec, parse.rs line 1302)
 *     - Layout: real part at byte 0, imaginary part at byte 8
 *
 *   struct S { _Complex float cf; _Complex double cd; }:
 *     - cf at offset 0: size=8, align=4
 *     - cd at offset 8: size=16, ABI align=4(i686)/8(LP64)
 *       Offset 8 satisfies both align=4 and align=8, so NO padding.
 *     - Total size = 8 + 16 = 24 bytes (no trailing padding needed)
 *     - Struct alignment = max(4, 4)=4 on i686, max(4, 8)=8 on LP64
 *     - sizeof(struct S) = 24 on ALL architectures
 *
 * ABI return path: 24-byte struct is > 16 bytes, so on x86-64 SysV it's
 * returned via hidden pointer (rdi). On AArch64/RISC-V, returned via
 * memory pointer. On i686, via hidden pointer on stack.
 *
 * Uses __real__ and __imag__ GCC extensions for complex component access
 * (supported by CCC: src/frontend/lexer/token.rs lines 93-95, 446-447).
 * Uses __alignof__ GCC extension for preferred alignment queries.
 * Uses offsetof CCC builtin macro (src/frontend/preprocessor/builtin_macros.rs).
 *
 * Architecture independence: all tests produce identical stdout on all 4
 * targets. No expected.skip.* files are needed.
 */

#include <stdio.h>

struct S {
    _Complex float cf;
    _Complex double cd;
};

/* ---------------------------------------------------------------------------
 * make_s
 *
 * Constructs a struct S from individual real/imaginary components.
 * Returns the struct by value, testing the return-value ABI path for a
 * 24-byte struct containing complex members.
 *
 * __attribute__((noinline)) prevents the optimizer from inlining the call
 * and bypassing the actual ABI parameter/return mechanism.
 * --------------------------------------------------------------------------- */
__attribute__((noinline))
struct S make_s(float cf_r, float cf_i, double cd_r, double cd_i) {
    struct S s;
    __real__ s.cf = cf_r;
    __imag__ s.cf = cf_i;
    __real__ s.cd = cd_r;
    __imag__ s.cd = cd_i;
    return s;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(_Complex float) == 8
     * _Complex float = 2 x sizeof(float) = 2 x 4 = 8 bytes.
     * Constant across all architectures. */
    if (sizeof(_Complex float) == 8) {
        printf("sizeof_complex_float: OK\n");
    } else {
        printf("sizeof_complex_float: FAIL (got %lu, expected 8)\n",
               (unsigned long)sizeof(_Complex float));
        failures++;
    }

    /* Test 2: sizeof(_Complex double) == 16
     * _Complex double = 2 x sizeof(double) = 2 x 8 = 16 bytes.
     * Constant across all architectures. */
    if (sizeof(_Complex double) == 16) {
        printf("sizeof_complex_double: OK\n");
    } else {
        printf("sizeof_complex_double: FAIL (got %lu, expected 16)\n",
               (unsigned long)sizeof(_Complex double));
        failures++;
    }

    /* Test 3: sizeof(struct S) == 24
     * cf(8) + cd(16) = 24, no padding needed between or after members.
     * 24 is a multiple of both 4 (i686 struct align) and 8 (LP64 struct align). */
    if (sizeof(struct S) == 24) {
        printf("sizeof_struct: OK\n");
    } else {
        printf("sizeof_struct: FAIL (got %lu, expected 24)\n",
               (unsigned long)sizeof(struct S));
        failures++;
    }

    /* Test 4: offsetof(struct S, cf) == 0
     * First field is always at offset 0. */
    if (offsetof(struct S, cf) == 0) {
        printf("offsetof_cf: OK\n");
    } else {
        printf("offsetof_cf: FAIL (got %lu, expected 0)\n",
               (unsigned long)offsetof(struct S, cf));
        failures++;
    }

    /* Test 5: offsetof(struct S, cd) == 8
     * After cf (8 bytes), cd at offset 8.
     * Offset 8 is divisible by both align=4 (i686) and align=8 (LP64),
     * so no padding is inserted between cf and cd. */
    if (offsetof(struct S, cd) == 8) {
        printf("offsetof_cd: OK\n");
    } else {
        printf("offsetof_cd: FAIL (got %lu, expected 8)\n",
               (unsigned long)offsetof(struct S, cd));
        failures++;
    }

    /* Test 6: __alignof__(_Complex float) == 4
     * ComplexFloat preferred alignment = 4 on all architectures.
     * (No preferred override for ComplexFloat on i686.) */
    if (__alignof__(_Complex float) == 4) {
        printf("alignof_cf: OK\n");
    } else {
        printf("alignof_cf: FAIL (got %lu, expected 4)\n",
               (unsigned long)__alignof__(_Complex float));
        failures++;
    }

    /* Test 7: __alignof__(_Complex double) == 8
     * __alignof__ returns PREFERRED alignment, not ABI minimum alignment.
     * On i686, preferred alignment for _Complex double is 8 (override in
     * preferred_align_ctx / preferred_alignof_type_spec), same as double.
     * On LP64, preferred == ABI alignment = 8.
     * So __alignof__(_Complex double) == 8 on ALL architectures. */
    if (__alignof__(_Complex double) == 8) {
        printf("alignof_cd: OK\n");
    } else {
        printf("alignof_cd: FAIL (got %lu, expected 8)\n",
               (unsigned long)__alignof__(_Complex double));
        failures++;
    }

    /* Test 8: __alignof__(struct S) -- architecture-dependent
     * Struct alignment = max(ABI_align(cf), ABI_align(cd)).
     * On i686: max(4, 4) = 4 (struct layout uses ABI alignment, not preferred).
     * On LP64: max(4, 8) = 8.
     * __alignof__ on a struct returns the struct's stored alignment from layout,
     * which is computed from ABI alignments of members.
     * Use sizeof(void*) as runtime proxy for pointer size. */
    {
        unsigned long expected_align = (sizeof(void*) == 4) ? 4 : 8;
        if (__alignof__(struct S) == expected_align) {
            printf("alignof_struct: OK\n");
        } else {
            printf("alignof_struct: FAIL (got %lu, expected %lu)\n",
                   (unsigned long)__alignof__(struct S), expected_align);
            failures++;
        }
    }

    /* Test 9: Value roundtrip through function return
     * Calls make_s with four IEEE 754 exactly-representable values,
     * then verifies all four components survive the struct return ABI.
     * 1.5f = 1 + 0.5, 2.5f = 2 + 0.5, 3.25 = 3 + 0.25, 4.75 = 4 + 0.5 + 0.25
     * All are exact sums of powers of 2, so == comparison is safe. */
    {
        struct S s = make_s(1.5f, 2.5f, 3.25, 4.75);
        if (__real__ s.cf == 1.5f && __imag__ s.cf == 2.5f &&
            __real__ s.cd == 3.25 && __imag__ s.cd == 4.75) {
            printf("return_value: OK\n");
        } else {
            printf("return_value: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All complex float member tests passed\n");

    return failures;
}
