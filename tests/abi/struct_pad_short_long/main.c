/*
 * ABI integration test: struct { short a; long b; } padding verification
 *
 * Verifies that CCC correctly computes the layout of a struct containing
 * a short followed by a long, focusing on the padding inserted between
 * short (2 bytes) and long to satisfy long's alignment requirement.
 *
 * Architecture-dependent layout:
 *
 *   LP64 targets (x86-64, AArch64, RISC-V 64) where ptr_sz=8:
 *     - short a:  offset=0, size=2, align=2
 *     - padding:  6 bytes (to align long to 8-byte boundary)
 *     - long b:   offset=8, size=8, align=8
 *     - sizeof = 16, alignof = 8
 *
 *   ILP32 (i686) where ptr_sz=4:
 *     - short a:  offset=0, size=2, align=2
 *     - padding:  2 bytes (to align long to 4-byte boundary)
 *     - long b:   offset=4, size=4, align=4
 *     - sizeof = 8, alignof = 4
 *
 * The test detects the target architecture at runtime via sizeof(void*)
 * and adjusts expected values accordingly, so it works on ALL four CCC
 * target architectures without any expected.skip.* files.
 *
 * References:
 *   src/common/types.rs line 1306 — CType::Short size=2
 *   src/common/types.rs line 1340 — CType::Short align=2
 *   src/common/types.rs line 1309 — CType::Long size=ptr_sz
 *   src/common/types.rs line 1343 — CType::Long align=ptr_sz
 */

#include <stdio.h>

/* Struct definition at file scope — matches AAP: struct { short a; long b; } */
struct PadShortLong {
    short a;
    long b;
};

/*
 * Noinline helper function to construct and return a PadShortLong struct.
 * __attribute__((noinline)) prevents the optimizer from inlining this call,
 * ensuring the actual ABI mechanism for returning structs is exercised.
 */
__attribute__((noinline))
struct PadShortLong make_pad_short_long(short a, long b) {
    struct PadShortLong s;
    s.a = a;
    s.b = b;
    return s;
}

int main(void) {
    int failures = 0;

    /* Compute architecture-dependent expected values.
     * On LP64: long size=8, align=8 -> 6 bytes padding after short, sizeof=16, offsetof(b)=8
     * On ILP32 (i686): long size=4, align=4 -> 2 bytes padding after short, sizeof=8, offsetof(b)=4
     */
    int is_64bit = (sizeof(void*) == 8);
    int expected_sizeof = is_64bit ? 16 : 8;
    int expected_offsetof_b = is_64bit ? 8 : 4;
    int expected_alignof = is_64bit ? 8 : 4;

    /* Test 1: sizeof(struct PadShortLong)
     * Checks sizeof==16 on LP64 (6 bytes padding between short and long)
     * or sizeof==8 on ILP32 (2 bytes padding).
     */
    if ((int)sizeof(struct PadShortLong) == expected_sizeof)
        printf("sizeof_PadShortLong: OK\n");
    else {
        printf("sizeof_PadShortLong: FAIL (got %d, expected %d)\n",
               (int)sizeof(struct PadShortLong), expected_sizeof);
        failures++;
    }

    /* Test 2: offsetof(struct PadShortLong, a) == 0
     * First field always at offset 0 — architecture-independent.
     */
    if ((int)offsetof(struct PadShortLong, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n",
               (int)offsetof(struct PadShortLong, a));
        failures++;
    }

    /* Test 3: offsetof(struct PadShortLong, b) — THE KEY ASSERTION
     * Verifies padding: offsetof(b)==8 on LP64 (6 bytes padding after
     * short to meet long's 8-byte alignment), or offsetof(b)==4 on i686
     * (2 bytes padding, long align=4).
     */
    if ((int)offsetof(struct PadShortLong, b) == expected_offsetof_b)
        printf("offsetof_b: OK\n");
    else {
        printf("offsetof_b: FAIL (got %d, expected %d)\n",
               (int)offsetof(struct PadShortLong, b), expected_offsetof_b);
        failures++;
    }

    /* Test 4: __alignof__(struct PadShortLong)
     * Struct alignment is max of members' alignment:
     *   alignof(short)=2, alignof(long)=8 on LP64 / 4 on i686.
     *   max(2, 8)=8 on LP64; max(2, 4)=4 on i686.
     */
    if ((int)__alignof__(struct PadShortLong) == expected_alignof)
        printf("alignof_PadShortLong: OK\n");
    else {
        printf("alignof_PadShortLong: FAIL (got %d, expected %d)\n",
               (int)__alignof__(struct PadShortLong), expected_alignof);
        failures++;
    }

    /* Test 5: Value correctness through noinline function return
     * Verify both fields survive the struct return ABI path.
     * Values: 7 fits in short, 123456789L fits in 4-byte long (max ~2.1B).
     */
    {
        struct PadShortLong s = make_pad_short_long(7, 123456789L);
        if (s.a == 7 && s.b == 123456789L)
            printf("value_check: OK\n");
        else {
            printf("value_check: FAIL (a=%d, b=%ld)\n", (int)s.a, s.b);
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct_pad_short_long tests passed\n");
    return failures;
}
