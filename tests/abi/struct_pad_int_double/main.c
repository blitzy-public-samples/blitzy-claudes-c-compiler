/*
 * ABI integration test: struct { int a; double b; } padding verification
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * an int followed by a double, focusing on the padding inserted between
 * int and double to satisfy double's alignment requirement.
 *
 * Architecture-dependent layout:
 *
 *   64-bit targets (x86-64, AArch64, RISC-V 64):
 *     - int a:    offset=0, size=4, align=4
 *     - padding:  4 bytes (to align double to 8-byte boundary)
 *     - double b: offset=8, size=8, align=8
 *     - sizeof = 16, alignof = 8
 *
 *   i686 (ptr_sz=4):
 *     - int a:    offset=0, size=4, align=4
 *     - double b: offset=4, size=8, align=4 (no padding needed)
 *     - sizeof = 12, alignof = 4
 *
 * The test detects the target architecture at runtime via sizeof(void*)
 * and adjusts expected values accordingly, so it works on ALL four CCC
 * target architectures without any expected.skip.* files.
 *
 * References:
 *   src/common/types.rs lines 1307,1313 — CType::Int size=4, CType::Double size=8
 *   src/common/types.rs lines 1341,1349 — CType::Int align=4, CType::Double align varies
 */

#include <stdio.h>

/* Struct definition at file scope — matches AAP: struct { int a; double b; } */
struct PadIntDouble {
    int a;
    double b;
};

/*
 * Noinline helper function to construct and return a PadIntDouble struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadIntDouble make_pad_int_double(int a, double b) {
    struct PadIntDouble s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Compute architecture-dependent expected values.
     * On 64-bit: double align=8 -> padding inserted, sizeof=16, offsetof(b)=8
     * On i686:   double align=4 -> no padding, sizeof=12, offsetof(b)=4
     */
    int is_64bit = (sizeof(void*) == 8);
    int expected_sizeof = is_64bit ? 16 : 12;
    int expected_offsetof_b = is_64bit ? 8 : 4;
    int expected_alignof = is_64bit ? 8 : 4;

    /* Test 1: sizeof(struct PadIntDouble)
     * Checks sizeof==16 on 64-bit (4 bytes padding between int and double)
     * or sizeof==12 on i686 (no padding, double align=4).
     */
    if ((int)sizeof(struct PadIntDouble) == expected_sizeof)
        printf("sizeof_PadIntDouble: OK\n");
    else {
        printf("sizeof_PadIntDouble: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct PadIntDouble), expected_sizeof);
        failures++;
    }

    /* Test 2: offsetof(struct PadIntDouble, a) == 0
     * First field always at offset 0 — architecture-independent.
     */
    if ((int)offsetof(struct PadIntDouble, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadIntDouble, a));
        failures++;
    }

    /* Test 3: offsetof(struct PadIntDouble, b) — THE KEY ASSERTION
     * Verifies padding: offsetof(b)==8 on 64-bit (4 bytes padding after
     * int to meet double's 8-byte alignment), or offsetof(b)==4 on i686
     * (no padding, double align=4).
     */
    if ((int)offsetof(struct PadIntDouble, b) == expected_offsetof_b)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected %d)\n",
               (int)offsetof(struct PadIntDouble, b), expected_offsetof_b);
        failures++;
    }

    /* Test 4: __alignof__(struct PadIntDouble)
     * Struct alignment is max of members' alignment:
     *   alignof(int)=4, alignof(double)=8 on 64-bit / 4 on i686.
     */
    if ((int)__alignof__(struct PadIntDouble) == expected_alignof)
        printf("alignof_PadIntDouble: OK\n");
    else {
        printf("alignof_PadIntDouble: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct PadIntDouble), expected_alignof);
        failures++;
    }

    /* Test 5: Value correctness through noinline function return
     * Verify both fields survive the struct return ABI path.
     */
    {
        struct PadIntDouble s = make_pad_int_double(42, 3.14);
        if (s.a == 42 && s.b == 3.14)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%d, b=%f)\n", s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_int_double tests passed\n");
    return failures;
}
