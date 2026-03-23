/*
 * ABI integration test: struct layout with various pointer type members
 *
 * Verifies that CCC correctly implements struct layout when all members
 * are pointer types (char *, int *, void *, function pointer) across all
 * four target architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * Key invariants tested:
 *   - All data pointer types (char*, int*, void*) and function pointers
 *     have the same size and alignment (ptr_sz) on every architecture.
 *   - No padding exists between consecutive pointer-sized, pointer-aligned
 *     fields, so sizeof(struct S) == 4 * sizeof(void*).
 *   - Pointer values stored through struct members survive load/store.
 *   - Function pointer members can be called via indirect call.
 *
 * Layout on LP64 (x86-64, AArch64, RISC-V 64):
 *   ptr_sz = 8
 *   struct S {
 *       char *a;            // offset  0, size 8, align 8
 *       int *b;             // offset  8, size 8, align 8
 *       void *c;            // offset 16, size 8, align 8
 *       void (*fp)(void);   // offset 24, size 8, align 8
 *   };
 *   sizeof(struct S) = 32, alignof = 8, padding = 0
 *
 * Layout on ILP32 (i686):
 *   ptr_sz = 4
 *   struct S {
 *       char *a;            // offset  0, size 4, align 4
 *       int *b;             // offset  4, size 4, align 4
 *       void *c;            // offset  8, size 4, align 4
 *       void (*fp)(void);   // offset 12, size 4, align 4
 *   };
 *   sizeof(struct S) = 16, alignof = 4, padding = 0
 *
 * Per src/common/types.rs:
 *   CType::Pointer(_, _) => ptr_sz   (size and alignment)
 *   CType::Function(_)   => ptr_sz   (size and alignment)
 */

#include <stdio.h>

struct S {
    char *a;
    int *b;
    void *c;
    void (*fp)(void);
};

static int g_called = 0;

__attribute__((noinline))
static void callback(void) {
    g_called = 1;
}

int main(void) {
    int failures = 0;

    /* Test 1: All pointer types have the same size */
    if (sizeof(char *) == sizeof(int *) &&
        sizeof(int *) == sizeof(void *) &&
        sizeof(void *) == sizeof(void (*)(void)))
        printf("ptr_sizes_equal: OK\n");
    else {
        printf("ptr_sizes_equal: FAIL (char*=%zu, int*=%zu, void*=%zu, fp=%zu)\n",
               sizeof(char *), sizeof(int *), sizeof(void *), sizeof(void (*)(void)));
        failures++;
    }

    /* Test 2: struct S size equals 4 * pointer size (no padding) */
    if (sizeof(struct S) == 4 * sizeof(void *))
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (struct=%zu, expected=%zu)\n",
               sizeof(struct S), 4 * sizeof(void *));
        failures++;
    }

    /* Test 3: field a at offset 0 */
    {
        struct S s;
        int offset = (int)((char *)&s.a - (char *)&s);
        if (offset == 0)
            printf("offset_a: OK\n");
        else {
            printf("offset_a: FAIL (offset=%d)\n", offset);
            failures++;
        }
    }

    /* Test 4: field b at offset sizeof(void*) */
    {
        struct S s;
        int offset = (int)((char *)&s.b - (char *)&s);
        int expected = (int)sizeof(void *);
        if (offset == expected)
            printf("offset_b: OK\n");
        else {
            printf("offset_b: FAIL (offset=%d, expected=%d)\n", offset, expected);
            failures++;
        }
    }

    /* Test 5: field c at offset 2*sizeof(void*) */
    {
        struct S s;
        int offset = (int)((char *)&s.c - (char *)&s);
        int expected = 2 * (int)sizeof(void *);
        if (offset == expected)
            printf("offset_c: OK\n");
        else {
            printf("offset_c: FAIL (offset=%d, expected=%d)\n", offset, expected);
            failures++;
        }
    }

    /* Test 6: field fp at offset 3*sizeof(void*) */
    {
        struct S s;
        int offset = (int)((char *)&s.fp - (char *)&s);
        int expected = 3 * (int)sizeof(void *);
        if (offset == expected)
            printf("offset_fp: OK\n");
        else {
            printf("offset_fp: FAIL (offset=%d, expected=%d)\n", offset, expected);
            failures++;
        }
    }

    /* Test 7: Store and retrieve pointer values through struct members */
    {
        char ch = 'X';
        int val = 42;
        struct S s;
        s.a = &ch;
        s.b = &val;
        s.c = &s;
        s.fp = callback;
        if (*s.a == 'X' && *s.b == 42 && s.c == (void *)&s && s.fp == callback)
            printf("store_load_ptrs: OK\n");
        else {
            printf("store_load_ptrs: FAIL\n");
            failures++;
        }
    }

    /* Test 8: Call function pointer through struct member */
    {
        struct S s;
        g_called = 0;
        s.a = 0;
        s.b = 0;
        s.c = 0;
        s.fp = callback;
        s.fp();
        if (g_called == 1)
            printf("call_fp: OK\n");
        else {
            printf("call_fp: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct pointer member tests passed\n");
    return failures;
}
