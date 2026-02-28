/*
 * ABI integration test: struct { char a; int b; char c; double d; short e; }
 *
 * Verifies that CCC correctly computes the layout of a struct with five
 * fields of different sizes and alignment requirements. This exercises
 * the complex multi-field padding scenario:
 *   - Padding between char a and int b (char→int gap)
 *   - Padding between char c and double d (char→double gap)
 *   - Tail padding after short e to meet struct alignment
 *
 * Architecture-dependent layout:
 *
 *   64-bit targets (x86-64, AArch64, RISC-V 64):
 *     - char a:   offset=0,  size=1, align=1
 *     - padding:  3 bytes (to align int b to 4-byte boundary)
 *     - int b:    offset=4,  size=4, align=4
 *     - char c:   offset=8,  size=1, align=1
 *     - padding:  7 bytes (to align double d to 8-byte boundary)
 *     - double d: offset=16, size=8, align=8
 *     - short e:  offset=24, size=2, align=2
 *     - padding:  6 bytes (tail padding to make sizeof multiple of 8)
 *     - sizeof = 32, alignof = 8
 *
 *   i686 (ptr_sz=4):
 *     - char a:   offset=0,  size=1, align=1
 *     - padding:  3 bytes (to align int b to 4-byte boundary)
 *     - int b:    offset=4,  size=4, align=4
 *     - char c:   offset=8,  size=1, align=1
 *     - padding:  3 bytes (to align double d to 4-byte boundary on i686)
 *     - double d: offset=12, size=8, align=4
 *     - short e:  offset=20, size=2, align=2
 *     - padding:  2 bytes (tail padding to make sizeof multiple of 4)
 *     - sizeof = 24, alignof = 4
 *
 * The test detects the target architecture at runtime via sizeof(void*)
 * and adjusts expected values accordingly, so it works on ALL four CCC
 * target architectures without any expected.skip.* files.
 *
 * References:
 *   src/common/types.rs lines 1305-1306 — CType::Char size=1, align=1
 *   src/common/types.rs lines 1307,1341 — CType::Int size=4, align=4
 *   src/common/types.rs lines 1313,1349 — CType::Double size=8, align=4|8
 *   src/common/types.rs lines 1306,1340 — CType::Short size=2, align=2
 */

#include <stdio.h>
#include <stddef.h>

/* Struct definition at file scope — matches AAP: struct { char a; int b; char c; double d; short e; } */
struct PadMultiField {
    char a;
    int b;
    char c;
    double d;
    short e;
};

/*
 * Noinline helper function to construct and return a PadMultiField struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadMultiField make_pad_multi_field(char a, int b, char c, double d, short e) {
    struct PadMultiField s;
    s.a = a;
    s.b = b;
    s.c = c;
    s.d = d;
    s.e = e;
    return s;
}

int main(void) {
    int failures = 0;

    /* Compute architecture-dependent expected values.
     * On 64-bit: double align=8 -> more padding, sizeof=32
     * On i686:   double align=4 -> less padding, sizeof=24
     */
    int is_64bit = (sizeof(void*) == 8);
    int expected_sizeof    = is_64bit ? 32 : 24;
    int expected_offsetof_d = is_64bit ? 16 : 12;
    int expected_offsetof_e = is_64bit ? 24 : 20;
    int expected_alignof   = is_64bit ? 8 : 4;

    /* Test 1: sizeof(struct PadMultiField)
     * Checks sizeof==32 on 64-bit (7 bytes padding between char c and double d)
     * or sizeof==24 on i686 (3 bytes padding, double align=4).
     */
    if ((int)sizeof(struct PadMultiField) == expected_sizeof)
        printf("sizeof_PadMultiField: OK\n");
    else {
        printf("sizeof_PadMultiField: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct PadMultiField), expected_sizeof);
        failures++;
    }

    /* Test 2: offsetof(struct PadMultiField, a) == 0
     * First field always at offset 0 — architecture-independent.
     */
    if ((int)offsetof(struct PadMultiField, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadMultiField, a));
        failures++;
    }

    /* Test 3: offsetof(struct PadMultiField, b) == 4
     * Int requires 4-byte alignment; char a (size 1) needs 3 bytes padding.
     * Architecture-independent (int align=4 on all targets).
     */
    if ((int)offsetof(struct PadMultiField, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected 4)\n",
               (int)offsetof(struct PadMultiField, b));
        failures++;
    }

    /* Test 4: offsetof(struct PadMultiField, c) == 8
     * Char c follows int b (offset 4 + size 4 = 8); char align=1, no padding.
     * Architecture-independent.
     */
    if ((int)offsetof(struct PadMultiField, c) == 8)
        printf("offsetof_c: OK\n");
    else {
        printf("offsetof_c: FAIL (got %d, expected 8)\n",
               (int)offsetof(struct PadMultiField, c));
        failures++;
    }

    /* Test 5: offsetof(struct PadMultiField, d) — THE KEY ASSERTION #1
     * On 64-bit: offset 16 (7 bytes padding after char c to align double to 8).
     * On i686:   offset 12 (3 bytes padding after char c to align double to 4).
     */
    if ((int)offsetof(struct PadMultiField, d) == expected_offsetof_d)
        printf("offsetof_d: OK\n");
    else {
        printf("offsetof_d: FAIL (got %d, expected %d)\n",
               (int)offsetof(struct PadMultiField, d), expected_offsetof_d);
        failures++;
    }

    /* Test 6: offsetof(struct PadMultiField, e) — THE KEY ASSERTION #2
     * On 64-bit: offset 24 (16 + 8). On i686: offset 20 (12 + 8).
     * Short align=2; both values are already even, so no extra padding.
     */
    if ((int)offsetof(struct PadMultiField, e) == expected_offsetof_e)
        printf("offsetof_e: OK\n");
    else {
        printf("offsetof_e: FAIL (got %d, expected %d)\n",
               (int)offsetof(struct PadMultiField, e), expected_offsetof_e);
        failures++;
    }

    /* Test 7: __alignof__(struct PadMultiField)
     * Struct alignment is max of all members' ABI alignment.
     * On 64-bit: max(1,4,1,8,2) = 8. On i686: max(1,4,1,4,2) = 4.
     */
    if ((int)__alignof__(struct PadMultiField) == expected_alignof)
        printf("alignof_PadMultiField: OK\n");
    else {
        printf("alignof_PadMultiField: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct PadMultiField), expected_alignof);
        failures++;
    }

    /* Test 8: Value correctness through noinline function return.
     * Verify all 5 fields survive the struct return ABI path.
     * Uses 3.14 for double — IEEE 754 rounds identically on both sides
     * since the same literal constant is used in both the call and comparison.
     */
    {
        struct PadMultiField s = make_pad_multi_field(1, 42, 2, 3.14, 99);
        if (s.a == 1 && s.b == 42 && s.c == 2 && s.d == 3.14 && s.e == 99)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%d, b=%d, c=%d, d=%f, e=%d)\n",
                   s.a, s.b, s.c, s.d, s.e);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_multi_field tests passed\n");
    return failures;
}
