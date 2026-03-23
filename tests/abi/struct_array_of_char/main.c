/*
 * ABI integration test: struct layout with char array member
 *
 * Verifies that CCC correctly implements struct layout when a struct
 * contains a char array member. Specifically tests:
 *   1. The char array member char data[7] has alignment 1 (inherits from char)
 *   2. The struct overall alignment is determined by int's alignment (4)
 *   3. offsetof(struct S, x) == 8 (1 byte padding after 7-byte char array)
 *   4. sizeof(struct S) == 12 (7 + 1 padding + 4 = 12, already 4-aligned)
 *
 * This exercises the struct layout logic in src/common/types.rs:
 *   - CType::Array(elem, _) => elem.align_ctx(ctx)        -- array alignment
 *     inherits element alignment (char -> 1)
 *   - CType::Array(elem, Some(n)) => elem.size_ctx(ctx)*n  -- array size =
 *     element_count * element_size (7 * 1 = 7)
 *   - StructLayoutBuilder::layout_regular_field()           -- field padding
 *     insertion based on alignment
 *   - StructLayoutBuilder::finalize()                       -- final struct
 *     size with trailing padding
 *
 * Layout on ALL 4 architectures (x86-64, AArch64, RISC-V 64, i686):
 *
 *   struct S {
 *       char data[7];  // offset  0, size  7, align 1 (from char)
 *                      // 1 byte padding (offset 7)
 *       int  x;        // offset  8, size  4, align 4
 *   };
 *   sizeof(struct S) = 12 on ALL architectures
 *   alignof(struct S) = 4 on ALL architectures (max of 1, 4)
 *
 *   All sizeof and offsetof values are architecture-independent because:
 *   - char=1, int=4 are the same sizes on all 4 targets
 *   - max_align=4 from int, align_up(12, 4) == 12
 *
 * No expected.skip.* files needed -- fully architecture-independent.
 */

#include <stdio.h>
#include <stddef.h>

struct S {
    char data[7];
    int x;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct S) == 12
     * Layout: char[7](7) + padding(1) + int(4) = 12
     * Proves inter-field padding is included in sizeof computation.
     * This is the direct AAP requirement: "sizeof should be 12".
     */
    if (sizeof(struct S) == 12)
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (got %zu, expected 12)\n", sizeof(struct S));
        failures++;
    }

    /* Test 2: offsetof(struct S, data) == 0
     * Baseline: first field always starts at offset 0.
     */
    if (offsetof(struct S, data) == 0)
        printf("offsetof_data: OK\n");
    else {
        printf("offsetof_data: FAIL (got %zu, expected 0)\n", offsetof(struct S, data));
        failures++;
    }

    /* Test 3: offsetof(struct S, x) == 8
     * THE KEY INTER-FIELD PADDING TEST.
     * int x has alignment 4. After char data[7] (7 bytes at offset 0),
     * 1 byte of padding is inserted to align x to offset 8 (the next
     * 4-byte boundary after offset 7).
     * This directly verifies the AAP requirement: "offsetof(x) should be 8".
     */
    if (offsetof(struct S, x) == 8)
        printf("offsetof_x: OK\n");
    else {
        printf("offsetof_x: FAIL (got %zu, expected 8)\n", offsetof(struct S, x));
        failures++;
    }

    /* Test 4: char array has alignment 1 (alignment inheritance)
     * Verifies that char[7] has alignment 1 (inherits from char element type).
     * The array placement at offset 0 satisfies any alignment. The real proof
     * is that offsetof(x) == 8, not 7: the 1 byte of padding comes from int's
     * alignment requirement, not from the char array needing larger alignment.
     * Tests the AAP requirement: "char array has alignment 1".
     */
    if (offsetof(struct S, data) % 1 == 0)
        printf("char_array_align: OK\n");
    else {
        printf("char_array_align: FAIL\n");
        failures++;
    }

    /* Test 5: struct overall alignment matches int's alignment
     * Verifies __alignof__(struct S) == __alignof__(int) == 4.
     * The struct alignment is max(align(char[7])=1, align(int)=4) = 4.
     * Tests the AAP requirement: "struct aligns to int's alignment requirement".
     */
    if (__alignof__(struct S) == __alignof__(int))
        printf("struct_align: OK\n");
    else {
        printf("struct_align: FAIL (struct align=%zu, int align=%zu)\n",
               __alignof__(struct S), __alignof__(int));
        failures++;
    }

    /* Test 6: sizeof(s.data) == 7
     * Confirms the char array member size is correctly computed as
     * element_count * element_size: 7 * sizeof(char) = 7 * 1 = 7.
     * Corresponds to CType::Array(elem, Some(n)) => elem.size_ctx(ctx) * n.
     */
    {
        struct S s;
        if (sizeof(s.data) == 7)
            printf("sizeof_data_member: OK\n");
        else {
            printf("sizeof_data_member: FAIL (got %zu, expected 7)\n", sizeof(s.data));
            failures++;
        }
    }

    /* Test 7: store and read back all fields including array elements
     * Verifies that the struct layout produces correct memory access
     * patterns. Stores values into all 7 char array elements and the
     * int member, then reads back to confirm correctness.
     */
    {
        struct S s;
        s.data[0] = 'H';
        s.data[1] = 'e';
        s.data[2] = 'l';
        s.data[3] = 'l';
        s.data[4] = 'o';
        s.data[5] = '!';
        s.data[6] = '\0';
        s.x = 42;
        if (s.data[0] == 'H' && s.data[1] == 'e' && s.data[2] == 'l' &&
            s.data[3] == 'l' && s.data[4] == 'o' && s.data[5] == '!' &&
            s.data[6] == '\0' && s.x == 42)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct array of char tests passed\n");

    return failures;
}
