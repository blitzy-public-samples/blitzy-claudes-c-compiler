/*
 * ABI integration test: struct { char a; short b; } internal padding
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * a char followed by a short, specifically focusing on the 1 byte of
 * internal padding inserted between char a and short b to satisfy
 * short's 2-byte alignment requirement.
 *
 * Layout analysis (ALL 4 architectures — x86-64, AArch64, RISC-V 64, i686):
 *
 *   struct PadCharShort {
 *       char a;      // offset=0, size=1, align=1
 *       // 1 byte padding (to align short to 2-byte boundary)
 *       short b;     // offset=2, size=2, align=2
 *   };
 *
 *   sizeof  = 4   (1 char + 1 padding + 2 short = 4)
 *   alignof = 2   (max(align(char), align(short)) = max(1, 2) = 2)
 *   4 is already a multiple of 2, so no trailing padding.
 *
 * From src/common/types.rs:
 *   CType::Char  -> size=1, align=1 on ALL architectures
 *   CType::Short -> size=2, align=2 on ALL architectures
 *
 * This test is completely architecture-independent — all expected values
 * are hardcoded constants.  No expected.skip.* files are needed.
 *
 * The noinline helper function ensures struct return goes through the
 * actual ABI mechanism rather than being optimized away by inlining.
 */

#include <stdio.h>
#include <stddef.h>

struct PadCharShort {
    char a;
    short b;
};

__attribute__((noinline))
struct PadCharShort make_pad_char_short(char a, short b) {
    struct PadCharShort s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* sizeof(struct PadCharShort) == 4
     * char a: offset 0, size 1
     * 1 byte padding to align short b to 2-byte boundary
     * short b: offset 2, size 2
     * Total: 1 + 1 padding + 2 = 4
     * 4 is already a multiple of struct alignment (2), no trailing padding
     */
    if ((int)sizeof(struct PadCharShort) == 4)
        printf("sizeof_PadCharShort: OK\n");
    else {
        printf("sizeof_PadCharShort: FAIL (got %d, expected 4)\n",
               (int)sizeof(struct PadCharShort));
        failures++;
    }

    /* offsetof(a) == 0: first field always at offset 0 */
    if ((int)offsetof(struct PadCharShort, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadCharShort, a));
        failures++;
    }

    /* offsetof(b) == 2: THE KEY ASSERTION per AAP
     * char a occupies offset 0 with size 1.
     * short b requires alignment to 2: next multiple of 2 after offset 1 is 2.
     * So 1 byte of padding is inserted after char a, and short b starts at offset 2.
     */
    if ((int)offsetof(struct PadCharShort, b) == 2)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected 2)\n",
               (int)offsetof(struct PadCharShort, b));
        failures++;
    }

    /* alignof == 2: struct alignment is max of members' alignment
     * max(align(char), align(short)) = max(1, 2) = 2
     */
    if ((int)__alignof__(struct PadCharShort) == 2)
        printf("alignof_PadCharShort: OK\n");
    else {
        printf("alignof_PadCharShort: FAIL (got %d, expected 2)\n",
               (int)__alignof__(struct PadCharShort));
        failures++;
    }

    /* Value correctness through noinline function return */
    {
        struct PadCharShort s = make_pad_char_short('X', 1234);
        if (s.a == 'X' && s.b == 1234)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%c, b=%d)\n", s.a, (int)s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_char_short tests passed\n");
    return failures;
}
