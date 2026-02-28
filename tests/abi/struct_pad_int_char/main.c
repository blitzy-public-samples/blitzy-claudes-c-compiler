/*
 * ABI integration test: struct { int a; char b; } trailing padding verification
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * an int followed by a char, focusing on the trailing padding inserted
 * after char b to round the struct size up to the struct's overall
 * alignment (4 bytes, determined by the int member).
 *
 * Architecture-independent layout (ALL 4 targets: x86-64, AArch64, RISC-V 64, i686):
 *
 *     struct PadIntChar {
 *         int a;      // offset=0, size=4, align=4
 *         char b;     // offset=4, size=1, align=1
 *         // 3 bytes trailing padding (to round sizeof to next multiple of struct align=4)
 *     };
 *     // sizeof = 8, alignof = 4
 *
 * Key calculations:
 *   - Struct alignment = max(align(int), align(char)) = max(4, 1) = 4
 *   - Raw payload = 4 (int) + 1 (char) = 5 bytes
 *   - 5 rounded up to next multiple of 4 = 8
 *   - sizeof == 8 on ALL architectures
 *   - offsetof(b) == 4 on ALL architectures
 *   - alignof == 4 on ALL architectures
 *
 * This test is COMPLETELY architecture-independent because int (size=4,
 * align=4) and char (size=1, align=1) have identical properties across
 * all four CCC target architectures. No runtime detection or skip files
 * are needed.
 *
 * References:
 *   src/common/types.rs line 1307 — CType::Int size=4 on all targets
 *   src/common/types.rs line 1341 — CType::Int align=4 on all targets
 *   src/common/types.rs line 1305 — CType::Char size=1 on all targets
 *   src/common/types.rs line 1339 — CType::Char align=1 on all targets
 */

#include <stdio.h>
#include <stddef.h>

/* Struct definition at file scope — matches AAP: struct { int a; char b; } */
struct PadIntChar {
    int a;
    char b;
};

/*
 * Noinline helper function to construct and return a PadIntChar struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadIntChar make_pad_int_char(int a, char b) {
    struct PadIntChar s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct PadIntChar) == 8
     * int a: offset 0, size 4
     * char b: offset 4, size 1
     * 3 bytes trailing padding to align to struct alignment (4)
     * Total: 4 + 1 + 3 = 8
     */
    if ((int)sizeof(struct PadIntChar) == 8)
        printf("sizeof_PadIntChar: OK\n");
    else {
        printf("sizeof_PadIntChar: FAIL (got %d, expected 8)\n",
               (int)sizeof(struct PadIntChar));
        failures++;
    }

    /* Test 2: offsetof(a) == 0: first field always at offset 0 */
    if ((int)offsetof(struct PadIntChar, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadIntChar, a));
        failures++;
    }

    /* Test 3: offsetof(b) == 4: THE KEY ASSERTION per AAP
     * char b follows int a (size 4), with no internal padding needed
     * since char's alignment is 1.
     */
    if ((int)offsetof(struct PadIntChar, b) == 4)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected 4)\n",
               (int)offsetof(struct PadIntChar, b));
        failures++;
    }

    /* Test 4: alignof == 4: struct alignment is max of members' alignment
     * max(align(int), align(char)) = max(4, 1) = 4
     */
    if ((int)__alignof__(struct PadIntChar) == 4)
        printf("alignof_PadIntChar: OK\n");
    else {
        printf("alignof_PadIntChar: FAIL (got %d, expected 4)\n",
               (int)__alignof__(struct PadIntChar));
        failures++;
    }

    /* Test 5: Value correctness through noinline function return */
    {
        struct PadIntChar s = make_pad_int_char(42, 'Z');
        if (s.a == 42 && s.b == 'Z')
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%d, b=%c)\n", s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_int_char tests passed\n");
    return failures;
}
