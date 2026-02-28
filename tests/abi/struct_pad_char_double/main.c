/*
 * ABI integration test: struct { char a; double b; } padding verification
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * a char followed by a double, focusing on the padding inserted between
 * char a and double b to satisfy double's alignment requirement.
 *
 * Architecture-dependent layout:
 *
 *   64-bit targets (x86-64, AArch64, RISC-V 64):
 *     - char a:   offset=0, size=1, align=1
 *     - padding:  7 bytes (to align double to 8-byte boundary)
 *     - double b: offset=8, size=8, align=8
 *     - sizeof = 16, alignof = 8
 *
 *   i686 (ptr_sz=4):
 *     - char a:   offset=0, size=1, align=1
 *     - padding:  3 bytes (to align double to 4-byte boundary)
 *     - double b: offset=4, size=8, align=4
 *     - sizeof = 12, alignof = 4
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

/* Struct definition at file scope — matches AAP: struct { char a; double b; } */
struct PadCharDouble {
    char a;
    double b;
};

/*
 * Noinline helper function to construct and return a PadCharDouble struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadCharDouble make_pad_char_double(char a, double b) {
    struct PadCharDouble s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Compute architecture-dependent expected values.
     * On 64-bit: double align=8 -> 7 bytes padding after char, sizeof=16, offsetof(b)=8
     * On i686:   double align=4 -> 3 bytes padding after char, sizeof=12, offsetof(b)=4
     */
    int is_64bit = (sizeof(void*) == 8);
    int expected_sizeof = is_64bit ? 16 : 12;
    int expected_offsetof_b = is_64bit ? 8 : 4;
    int expected_alignof = is_64bit ? 8 : 4;

    /* Test 1: sizeof(struct PadCharDouble)
     * Checks sizeof==16 on 64-bit (7 bytes padding after char + 8 bytes double)
     * or sizeof==12 on i686 (3 bytes padding + 8 bytes double).
     */
    if ((int)sizeof(struct PadCharDouble) == expected_sizeof)
        printf("sizeof_PadCharDouble: OK\n");
    else {
        printf("sizeof_PadCharDouble: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct PadCharDouble), expected_sizeof);
        failures++;
    }

    /* Test 2: offsetof(struct PadCharDouble, a) == 0
     * First field always at offset 0 — architecture-independent.
     */
    if ((int)offsetof(struct PadCharDouble, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadCharDouble, a));
        failures++;
    }

    /* Test 3: offsetof(struct PadCharDouble, b) — THE KEY ASSERTION
     * Verifies padding: offsetof(b)==8 on 64-bit (7 bytes padding after
     * char to meet double's 8-byte alignment), or offsetof(b)==4 on i686
     * (3 bytes padding, double align=4).
     */
    if ((int)offsetof(struct PadCharDouble, b) == expected_offsetof_b)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected %d)\n",
               (int)offsetof(struct PadCharDouble, b), expected_offsetof_b);
        failures++;
    }

    /* Test 4: __alignof__(struct PadCharDouble)
     * Struct alignment is max of members' alignment:
     *   alignof(char)=1, alignof(double)=8 on 64-bit / 4 on i686.
     */
    if ((int)__alignof__(struct PadCharDouble) == expected_alignof)
        printf("alignof_PadCharDouble: OK\n");
    else {
        printf("alignof_PadCharDouble: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct PadCharDouble), expected_alignof);
        failures++;
    }

    /* Test 5: Value correctness through noinline function return
     * Verify both fields survive the struct return ABI path.
     */
    {
        struct PadCharDouble s = make_pad_char_double('A', 2.718);
        if (s.a == 'A' && s.b == 2.718)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%c, b=%f)\n", s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_char_double tests passed\n");
    return failures;
}
