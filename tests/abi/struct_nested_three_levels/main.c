/*
 * ABI integration test: alignment and size through three levels of struct nesting
 *
 * Verifies that CCC correctly propagates alignment requirements upward through
 * nested struct definitions and computes field offsets and total struct sizes
 * correctly across all four target architectures (x86-64, AArch64, RISC-V 64,
 * i686).
 *
 * The KEY insight tested here: the alignment of a struct is the maximum
 * alignment of all its fields, and this propagates upward through nesting.
 * When struct L2 contains a double (align=8 on 64-bit, align=4 on i686),
 * L2 inherits that alignment, and when L3 contains L2, L3 inherits L2's
 * alignment. This causes different padding and total sizes on i686 vs 64-bit.
 *
 * Struct definitions:
 *   struct L1 { char a; };
 *   struct L2 { struct L1 x; double y; };
 *   struct L3 { int w; struct L2 z; char q; };
 *
 * Layout on 64-bit targets (x86-64, AArch64, RISC-V 64) where ptr_sz=8:
 *
 *   L1: sizeof=1, alignof=1
 *   L2: x at 0 (1B) + 7B padding + y at 8 (8B) => sizeof=16, alignof=8
 *   L3: w at 0 (4B) + 4B padding + z at 8 (16B) + q at 24 (1B) + 7B tail
 *       => sizeof=32, alignof=8
 *
 * Layout on i686 where ptr_sz=4 (double alignment is 4, not 8):
 *
 *   L1: sizeof=1, alignof=1
 *   L2: x at 0 (1B) + 3B padding + y at 4 (8B) => sizeof=12, alignof=4
 *   L3: w at 0 (4B) + z at 4 (12B) + q at 16 (1B) + 3B tail
 *       => sizeof=20, alignof=4
 *
 * This exercises the struct layout logic in src/common/types.rs:
 *   - CType::Double => if ptr_sz == 4 { 4 } else { 8 }  [line 1349]
 *   - CType::Struct(key) => ctx.get_struct_layout(key)    [line 1358-1360]
 *   - StructLayoutBuilder field placement and finalize
 *
 * No expected.skip.* files needed — runtime sizeof(void*) detection
 * selects architecture-appropriate expected values.
 */

#include <stdio.h>

/* Three levels of nested structs as specified by the AAP */
struct L1 { char a; };
struct L2 { struct L1 x; double y; };
struct L3 { int w; struct L2 z; char q; };

/*
 * Construct and return a struct L3 with all fields set.
 * The noinline attribute prevents the optimizer from bypassing the actual
 * ABI struct return mechanism (large struct return via hidden pointer or
 * register pairs depending on architecture).
 */
__attribute__((noinline))
struct L3 make_l3(int w, char a, double y, char q) {
    struct L3 s;
    s.w = w;
    s.z.x.a = a;
    s.z.y = y;
    s.q = q;
    return s;
}

int main(void) {
    int failures = 0;

    /* Detect architecture at runtime: 8 for 64-bit, 4 for i686 */
    int is_64bit = (sizeof(void*) == 8);

    /* Test 1: sizeof(L1) == 1 on ALL architectures
     * struct L1 { char a; } — single char field, no padding needed.
     */
    if (sizeof(struct L1) == 1)
        printf("sizeof_L1: OK\n");
    else {
        printf("sizeof_L1: FAIL\n");
        failures++;
    }

    /* Test 2: sizeof(L2)
     * 64-bit: 1 (L1) + 7 padding + 8 (double @align=8) = 16
     * i686:   1 (L1) + 3 padding + 8 (double @align=4) = 12
     */
    {
        int expected = is_64bit ? 16 : 12;
        if ((int)sizeof(struct L2) == expected)
            printf("sizeof_L2: OK\n");
        else {
            printf("sizeof_L2: FAIL\n");
            failures++;
        }
    }

    /* Test 3: offsetof(L2, y)
     * 64-bit: align_up(1, 8) = 8  (double alignment 8)
     * i686:   align_up(1, 4) = 4  (double alignment 4)
     * Demonstrates double alignment forcing padding after nested L1 member.
     */
    {
        int expected = is_64bit ? 8 : 4;
        if ((int)offsetof(struct L2, y) == expected)
            printf("offsetof_L2_y: OK\n");
        else {
            printf("offsetof_L2_y: FAIL\n");
            failures++;
        }
    }

    /* Test 4: sizeof(L3)
     * THE KEY THREE-LEVEL NESTING TEST.
     * L2's alignment (from double) propagates to L3, affecting both
     * inter-field padding and tail padding.
     * 64-bit: 4 (int) + 4 pad + 16 (L2 @align=8) + 1 (char) + 7 pad = 32
     * i686:   4 (int) + 0 pad + 12 (L2 @align=4) + 1 (char) + 3 pad = 20
     */
    {
        int expected = is_64bit ? 32 : 20;
        if ((int)sizeof(struct L3) == expected)
            printf("sizeof_L3: OK\n");
        else {
            printf("sizeof_L3: FAIL\n");
            failures++;
        }
    }

    /* Test 5: offsetof(L3, z)
     * 64-bit: align_up(4, 8) = 8  (L2 inherits double's 8-byte alignment)
     * i686:   align_up(4, 4) = 4  (L2 inherits double's 4-byte alignment)
     * Proves alignment propagation: L2's alignment (from double) determines
     * L3's field placement of the L2 member.
     */
    {
        int expected = is_64bit ? 8 : 4;
        if ((int)offsetof(struct L3, z) == expected)
            printf("offsetof_L3_z: OK\n");
        else {
            printf("offsetof_L3_z: FAIL\n");
            failures++;
        }
    }

    /* Test 6: offsetof(L3, q)
     * 64-bit: 8 + 16 = 24  (offset of z + sizeof L2)
     * i686:   4 + 12 = 16  (offset of z + sizeof L2)
     */
    {
        int expected = is_64bit ? 24 : 16;
        if ((int)offsetof(struct L3, q) == expected)
            printf("offsetof_L3_q: OK\n");
        else {
            printf("offsetof_L3_q: FAIL\n");
            failures++;
        }
    }

    /* Test 7: Value correctness through three nesting levels
     * Call noinline make_l3 to force the struct return ABI path.
     * All four fields must survive: w (int), z.x.a (char through L1→L2→L3),
     * z.y (double through L2→L3), q (char).
     * 3.14 as a double literal: same rounded value on both store and compare
     * sides, so the equality check is exact.
     */
    {
        struct L3 s = make_l3(42, 'A', 3.14, 'Z');
        if (s.w == 42 && s.z.x.a == 'A' && s.z.y == 3.14 && s.q == 'Z')
            printf("value_three_levels: OK\n");
        else {
            printf("value_three_levels: FAIL\n");
            failures++;
        }
    }

    /* Test 8: Nested L1 size propagation
     * sizeof(s.z.x) must equal sizeof(struct L1) == 1 even when
     * accessed through three levels of nesting (L3→L2→L1).
     */
    {
        struct L3 s;
        if (sizeof(s.z.x) == 1)
            printf("nested_L1_size: OK\n");
        else {
            printf("nested_L1_size: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All nested three levels tests passed\n");

    return failures;
}
