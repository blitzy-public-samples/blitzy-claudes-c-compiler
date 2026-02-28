/*
 * ABI integration test: struct { double a; char b; } trailing padding verification
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * a double followed by a char, focusing on the trailing (tail) padding
 * inserted after char b to round the struct size up to the struct's
 * overall alignment (determined by the double member).
 *
 * Architecture-dependent layout:
 *
 *   64-bit targets (x86-64, AArch64, RISC-V 64):
 *     - double a: offset=0, size=8, align=8
 *     - char b:   offset=8, size=1, align=1
 *     - padding:  7 bytes trailing (to round sizeof to multiple of max align=8)
 *     - sizeof = 16, alignof = 8
 *
 *   i686 (ptr_sz=4):
 *     - double a: offset=0, size=8, align=4
 *     - char b:   offset=8, size=1, align=1
 *     - padding:  3 bytes trailing (to round sizeof to multiple of max align=4)
 *     - sizeof = 12, alignof = 4
 *
 * KEY INSIGHT: offsetof(b) == 8 on ALL architectures because double is
 * always 8 bytes in size regardless of alignment. Only sizeof and alignof
 * differ between i686 and 64-bit targets.
 *
 * The test detects the target architecture at runtime via sizeof(void*)
 * and adjusts expected values accordingly, so it works on ALL four CCC
 * target architectures without any expected.skip.* files.
 *
 * References:
 *   src/common/types.rs lines 1305,1313 — CType::Char size=1, CType::Double size=8
 *   src/common/types.rs lines 1339,1349 — CType::Char align=1, CType::Double align varies
 */

#include <stdio.h>

/* Struct definition at file scope — matches AAP: struct { double a; char b; } */
struct PadDoubleChar {
    double a;
    char b;
};

/*
 * Noinline helper function to construct and return a PadDoubleChar struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadDoubleChar make_pad_double_char(double a, char b) {
    struct PadDoubleChar s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Compute architecture-dependent expected values.
     * On 64-bit: double align=8 -> tail padding of 7, sizeof=16
     * On i686:   double align=4 -> tail padding of 3, sizeof=12
     */
    int is_64bit = (sizeof(void*) == 8);
    int expected_sizeof = is_64bit ? 16 : 12;
    int expected_alignof = is_64bit ? 8 : 4;

    /* Test 1: sizeof(struct PadDoubleChar)
     * Checks sizeof==16 on 64-bit (7 bytes trailing padding after char b)
     * or sizeof==12 on i686 (3 bytes trailing padding after char b).
     * This is THE KEY ASSERTION per the AAP.
     */
    if ((int)sizeof(struct PadDoubleChar) == expected_sizeof)
        printf("sizeof_PadDoubleChar: OK\n");
    else {
        printf("sizeof_PadDoubleChar: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct PadDoubleChar), expected_sizeof);
        failures++;
    }

    /* Test 2: offsetof(struct PadDoubleChar, a) == 0
     * First field always at offset 0 — architecture-independent.
     */
    if ((int)offsetof(struct PadDoubleChar, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadDoubleChar, a));
        failures++;
    }

    /* Test 3: offsetof(struct PadDoubleChar, b) == 8
     * THE SECOND KEY ASSERTION per the AAP.
     * offsetof(b)==8 on ALL architectures because double is always 8 bytes
     * in size, and char b follows immediately with align=1. This value is
     * architecture-independent — hardcoded comparison to 8.
     */
    if ((int)offsetof(struct PadDoubleChar, b) == 8)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected 8)\n",
               (int)offsetof(struct PadDoubleChar, b));
        failures++;
    }

    /* Test 4: __alignof__(struct PadDoubleChar)
     * Struct alignment is max of members' alignment:
     *   max(alignof(double), alignof(char)).
     * On 64-bit: max(8,1) = 8. On i686: max(4,1) = 4.
     */
    if ((int)__alignof__(struct PadDoubleChar) == expected_alignof)
        printf("alignof_PadDoubleChar: OK\n");
    else {
        printf("alignof_PadDoubleChar: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct PadDoubleChar), expected_alignof);
        failures++;
    }

    /* Test 5: Value correctness through noinline function return
     * Verify both fields survive the struct return ABI path.
     */
    {
        struct PadDoubleChar s = make_pad_double_char(3.14, 'X');
        if (s.a == 3.14 && s.b == 'X')
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%f, b=%c)\n", s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_double_char tests passed\n");
    return failures;
}
