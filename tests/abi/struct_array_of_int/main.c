/*
 * ABI integration test: struct layout with int array member
 *
 * Verifies that CCC correctly implements struct layout when a struct
 * contains an array member. Specifically tests:
 *   1. The array member int arr[3] inherits the alignment of its element
 *      type (int, align=4), proven by offsetof(S, arr) == 4.
 *   2. The sizeof computation correctly accounts for inter-field padding
 *      (3 bytes between char a and int arr[3]).
 *   3. Values stored through the struct are correctly read back, including
 *      all three array elements.
 *
 * This exercises the struct layout logic in src/common/types.rs:
 *   - CType::Array(elem, _) => elem.align_ctx(ctx)        [line 1356]
 *   - CType::Array(elem, Some(n)) => elem.size_ctx(ctx)*n  [line 1322]
 *   - StructLayoutBuilder::layout_regular_field()           [line 469]
 *   - StructLayoutBuilder::finalize()                       [line 493]
 *
 * Layout on ALL 4 architectures (x86-64, AArch64, RISC-V 64, i686):
 *
 *   struct S {
 *       char   a;       // offset  0, size  1, align 1
 *                       // 3 bytes padding (offsets 1-3)
 *       int    arr[3];  // offset  4, size 12, align 4  (inherits int align)
 *       double b;       // offset 16, size  8, align 8(64-bit)/4(i686)
 *   };
 *   sizeof(struct S) = 24 on ALL architectures
 *   alignof(struct S) = 8 on 64-bit, 4 on i686
 *
 *   All sizeof and offsetof values are architecture-independent because:
 *   - char=1, int=4, double=8 are the same sizes on all 4 targets
 *   - double alignment differs (8 vs 4) but offset 16 satisfies both
 *   - align_up(24, 8) == align_up(24, 4) == 24
 *
 * No expected.skip.* files needed — fully architecture-independent.
 */

#include <stdio.h>
#include <stddef.h>

struct S {
    char a;
    int arr[3];
    double b;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct S) == 24
     * Layout: char(1) + padding(3) + int[3](12) + double(8) = 24
     * Proves inter-field padding is included in sizeof computation.
     */
    if (sizeof(struct S) == 24)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %zu, expected 24)\n", sizeof(struct S));
        failures++;
    }

    /* Test 2: offsetof(struct S, a) == 0
     * Baseline: first field always starts at offset 0.
     */
    if (offsetof(struct S, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL\n");
        failures++;
    }

    /* Test 3: offsetof(struct S, arr) == 4
     * THE KEY INTER-FIELD PADDING TEST.
     * int arr[3] has alignment 4 (inherited from int element type).
     * After char a (1 byte at offset 0), 3 bytes of padding are
     * inserted to align arr to offset 4.
     */
    if (offsetof(struct S, arr) == 4)
        printf("offsetof_arr: OK\n");
    else {
        printf("offsetof_arr: FAIL\n");
        failures++;
    }

    /* Test 4: offsetof(struct S, b) == 16
     * double b starts at offset 4 + 12 = 16.
     * On 64-bit: align=8, offset 16 is 8-aligned => no padding.
     * On i686:   align=4, offset 16 is 4-aligned => no padding.
     */
    if (offsetof(struct S, b) == 16)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL\n");
        failures++;
    }

    /* Test 5: array alignment inherits element alignment
     * Verifies that the array offset is a multiple of __alignof__(int)
     * AND equals 4, proving the struct layout engine used int's alignment
     * for the int array field.
     */
    if (offsetof(struct S, arr) % __alignof__(int) == 0 && offsetof(struct S, arr) == 4)
        printf("arr_inherits_int_align: OK\n");
    else {
        printf("arr_inherits_int_align: FAIL\n");
        failures++;
    }

    /* Test 6: sizeof(s.arr) == 3 * sizeof(int)
     * Confirms array member size = element_count * element_size (12 = 3*4).
     * Corresponds to CType::Array(elem, Some(n)) => elem.size_ctx(ctx) * n.
     */
    {
        struct S s;
        if (sizeof(s.arr) == 3 * sizeof(int))
            printf("sizeof_arr_member: OK\n");
        else {
            printf("sizeof_arr_member: FAIL\n");
            failures++;
        }
    }

    /* Test 7: store and read back all fields including array elements
     * Verifies that the struct layout produces correct memory access
     * patterns. The double value 1.5 is exactly representable in IEEE 754.
     */
    {
        struct S s;
        s.a = 'X';
        s.arr[0] = 100;
        s.arr[1] = 200;
        s.arr[2] = 300;
        s.b = 1.5;
        if (s.a == 'X' && s.arr[0] == 100 && s.arr[1] == 200 && s.arr[2] == 300 && s.b == 1.5)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct array of int tests passed\n");

    return failures;
}
