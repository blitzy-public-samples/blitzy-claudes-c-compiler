/*
 * ABI integration test: packed struct + aligned member combo
 *
 * Verifies the interaction between __attribute__((packed)) on a struct
 * and __attribute__((aligned(N))) on an individual member field.
 *
 * This is a critical ABI corner case: packed eliminates natural padding
 * by capping field alignment to 1, but an explicit __attribute__((aligned(N)))
 * on a member overrides the struct-level packing and forces that field to
 * the specified alignment.
 *
 * Layout priority order in StructLayoutBuilder::compute_field_alignment():
 *   1. Per-field __attribute__((packed))       -> alignment = 1
 *   2. Explicit field.alignment (aligned(N))   -> natural_align.max(explicit)
 *      ** overrides struct-level packing **
 *   3. Struct-level packing (max_field_align)  -> natural_align.min(max_a)
 *   4. Default                                 -> natural_align
 *
 * Three structs are compared:
 *
 *   S_packed_aligned (packed + aligned(8) on field b):
 *     char a at offset 0 (size 1)
 *     7 bytes padding (to satisfy aligned(8))
 *     int b at offset 8 (size 4, aligned(8) overrides packed)
 *     4 bytes trailing padding (struct align = 8, size = align_up(12,8) = 16)
 *     sizeof = 16, alignof = 8
 *
 *   S_packed_only (packed, no aligned):
 *     char a at offset 0 (size 1)
 *     int b at offset 1 (size 4, alignment capped to 1 by packed)
 *     sizeof = 5, alignof = 1
 *
 *   S_default (no attributes):
 *     char a at offset 0 (size 1)
 *     3 bytes padding (natural int alignment = 4)
 *     int b at offset 4 (size 4)
 *     sizeof = 8, alignof = 4
 *
 * All sizeof and offsetof values are architecture-independent:
 *   char = 1 byte, int = 4 bytes on all 4 CCC targets
 *   (x86-64, AArch64, RISC-V 64, i686)
 *
 * Tests (7 total + summary):
 *   1. sizeof_packed_aligned       - sizeof == 16
 *   2. offsetof_b_packed_aligned   - offsetof(b) == 8 (THE KEY ASSERTION)
 *   3. sizeof_packed_only          - sizeof == 5
 *   4. offsetof_b_packed_only      - offsetof(b) == 1
 *   5. sizeof_default              - sizeof == 8
 *   6. offsetof_b_default          - offsetof(b) == 4
 *   7. value_correctness           - store/load through packed+aligned struct
 */

#include <stdio.h>

/* Struct 1: Packed + Aligned combo (THE KEY TEST)
 * packed caps field alignment to 1, but aligned(8) on field b overrides it.
 * Field b gets alignment 8 = max(natural_align=4, explicit=8). */
struct __attribute__((packed)) S_packed_aligned {
    char a;
    int b __attribute__((aligned(8)));
};

/* Struct 2: Packed only (no aligned — comparison baseline)
 * All field alignments capped to 1 by packed. */
struct __attribute__((packed)) S_packed_only {
    char a;
    int b;
};

/* Struct 3: Default layout (no packed, no aligned — baseline)
 * Natural alignment rules apply. */
struct S_default {
    char a;
    int b;
};

int main(void) {
    int failures = 0;

    /* Test 1: sizeof packed+aligned struct = 16
     * char a (1) + 7 pad + int b (4) + 4 trailing pad = 16
     * Struct align = 8 from aligned(8) on field b. */
    if (sizeof(struct S_packed_aligned) == 16)
        printf("sizeof_packed_aligned: OK\n");
    else {
        printf("sizeof_packed_aligned: FAIL (got %zu)\n", sizeof(struct S_packed_aligned));
        failures++;
    }

    /* Test 2: offsetof(b) = 8 in packed+aligned struct (THE KEY ASSERTION)
     * Explicit aligned(8) overrides struct-level packed.
     * Without aligned(8), packed would place b at offset 1. */
    if (offsetof(struct S_packed_aligned, b) == 8)
        printf("offsetof_b_packed_aligned: OK\n");
    else {
        printf("offsetof_b_packed_aligned: FAIL (got %zu)\n", offsetof(struct S_packed_aligned, b));
        failures++;
    }

    /* Test 3: sizeof packed-only struct = 5
     * char a (1) + int b (4) = 5, no padding (all aligned to 1). */
    if (sizeof(struct S_packed_only) == 5)
        printf("sizeof_packed_only: OK\n");
    else {
        printf("sizeof_packed_only: FAIL (got %zu)\n", sizeof(struct S_packed_only));
        failures++;
    }

    /* Test 4: offsetof(b) = 1 in packed-only struct
     * Packed caps int alignment from 4 to 1, so b follows a immediately. */
    if (offsetof(struct S_packed_only, b) == 1)
        printf("offsetof_b_packed_only: OK\n");
    else {
        printf("offsetof_b_packed_only: FAIL (got %zu)\n", offsetof(struct S_packed_only, b));
        failures++;
    }

    /* Test 5: sizeof default struct = 8
     * char a (1) + 3 pad + int b (4) = 8. */
    if (sizeof(struct S_default) == 8)
        printf("sizeof_default: OK\n");
    else {
        printf("sizeof_default: FAIL (got %zu)\n", sizeof(struct S_default));
        failures++;
    }

    /* Test 6: offsetof(b) = 4 in default struct
     * Natural alignment of int = 4. */
    if (offsetof(struct S_default, b) == 4)
        printf("offsetof_b_default: OK\n");
    else {
        printf("offsetof_b_default: FAIL (got %zu)\n", offsetof(struct S_default, b));
        failures++;
    }

    /* Test 7: Value correctness through packed+aligned struct
     * Verify that store and load through the mixed packed/aligned layout
     * produce correct values — no memory corruption from the unusual layout. */
    {
        struct S_packed_aligned s;
        s.a = 'X';
        s.b = 12345678;
        if (s.a == 'X' && s.b == 12345678)
            printf("value_correctness: OK\n");
        else {
            printf("value_correctness: FAIL\n");
            failures++;
        }
    }

    /* Summary */
    if (failures == 0)
        printf("All packed+aligned combo tests passed\n");
    return failures;
}
