/*
 * ABI integration test: struct { char a; int b; }
 *
 * Verifies that CCC correctly computes the layout of a struct with a char
 * field followed by an int field, focusing on the 3 bytes of internal padding
 * inserted between char a and int b to satisfy int's 4-byte alignment.
 *
 * Layout on ALL 4 architectures (x86-64, AArch64, RISC-V 64, i686):
 *   struct PadCharInt {
 *       char a;      // offset=0, size=1, align=1
 *       // 3 bytes padding (to align int to 4-byte boundary)
 *       int b;       // offset=4, size=4, align=4
 *   };
 *   sizeof  = 8  (1 char + 3 padding + 4 int = 8)
 *   alignof = 4  (max(align(char), align(int)) = max(1, 4) = 4)
 *   8 is already a multiple of 4, so no trailing padding.
 *
 * Architecture-independent: char size=1 align=1, int size=4 align=4
 * on all targets per src/common/types.rs lines 1305-1307 and 1339-1341.
 */

#include <stdio.h>

struct PadCharInt {
    char a;
    int b;
};

__attribute__((noinline))
struct PadCharInt make_pad_char_int(char a, int b) {
    struct PadCharInt s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* sizeof(struct PadCharInt) == 8
     * char a: offset 0, size 1
     * 3 bytes padding to align int b to 4-byte boundary
     * int b: offset 4, size 4
     * Total: 1 + 3 padding + 4 = 8
     * 8 is already a multiple of struct alignment (4), no trailing padding
     */
    if ((int)sizeof(struct PadCharInt) == 8)
        printf("sizeof_PadCharInt: OK\n");
    else {
        printf("sizeof_PadCharInt: FAIL (got %d, expected 8)\n",
               (int)sizeof(struct PadCharInt));
        failures++;
    }

    /* offsetof(a) == 0: first field always at offset 0 */
    if ((int)offsetof(struct PadCharInt, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadCharInt, a));
        failures++;
    }

    /* offsetof(b) == 4: THE KEY ASSERTION per AAP
     * char a occupies offset 0 with size 1.
     * int b requires alignment to 4: next multiple of 4 after offset 1 is 4.
     * So 3 bytes of padding are inserted after char a, and int b starts at offset 4.
     */
    if ((int)offsetof(struct PadCharInt, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected 4)\n",
               (int)offsetof(struct PadCharInt, b));
        failures++;
    }

    /* alignof == 4: struct alignment is max of members' alignment
     * max(align(char), align(int)) = max(1, 4) = 4
     */
    if ((int)__alignof__(struct PadCharInt) == 4)
        printf("alignof_PadCharInt: OK\n");
    else {
        printf("alignof_PadCharInt: FAIL (got %d, expected 4)\n",
               (int)__alignof__(struct PadCharInt));
        failures++;
    }

    /* Value correctness through noinline function return */
    {
        struct PadCharInt s = make_pad_char_int('X', 12345);
        if (s.a == 'X' && s.b == 12345)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%c, b=%d)\n", s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_char_int tests passed\n");
    return failures;
}
