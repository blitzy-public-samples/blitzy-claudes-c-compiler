/*
 * ABI integration test: struct layout with function pointer members
 *
 * Verifies that CCC correctly implements struct layout when members are
 * function pointers of various signatures, across all four target
 * architectures (x86-64, AArch64, RISC-V 64, i686).
 *
 * This exercises:
 *   1. Function pointer size equals data pointer size (sizeof(void*)).
 *      Per src/common/types.rs: CType::Function(_) => ptr_sz for both
 *      size_ctx and align_ctx, matching CType::Pointer(_, _) => ptr_sz.
 *
 *   2. Struct layout with three function pointer members of different
 *      signatures has NO padding, since all fields are pointer-sized and
 *      pointer-aligned:
 *        f1: int (*)(int, int)      at offset 0
 *        f2: void (*)(void)         at offset 1 * sizeof(void*)
 *        f3: char *(*)(const char*) at offset 2 * sizeof(void*)
 *        sizeof(struct S) = 3 * sizeof(void*)
 *
 *      LP64 (x86-64, AArch64, RISC-V 64): 24 bytes, 8-byte aligned
 *      ILP32 (i686):                       12 bytes, 4-byte aligned
 *
 *   3. Calling functions through struct member function pointers exercises
 *      the actual ABI calling convention for each signature:
 *        - int(int, int):      two GP register integer args, integer return
 *        - void(void):         no args, no return, side-effect via global
 *        - char*(const char*): pointer arg in GP register, pointer return
 *
 * All helper functions use __attribute__((noinline)) to prevent the
 * optimizer from inlining calls, ensuring the actual calling convention
 * and function pointer dispatch ABI is exercised.
 *
 * No architecture-specific preprocessor guards are used.  The test
 * produces identical output on all four architectures.
 */

#include <stdio.h>

static int g_flag = 0;

struct S {
    int (*f1)(int, int);
    void (*f2)(void);
    char *(*f3)(const char *);
};

/*
 * add_func: returns the sum of two integers.
 *
 * Used as the target of the int (*)(int, int) function pointer member f1.
 * Parameters passed in GP registers:
 *   x86-64:  edi, esi
 *   AArch64: w0, w1
 *   RISC-V:  a0, a1
 *   i686:    stack (cdecl)
 * Return value in eax / w0 / a0 / eax.
 */
__attribute__((noinline))
int add_func(int a, int b) {
    return a + b;
}

/*
 * set_flag: sets the global g_flag to 1.
 *
 * Used as the target of the void (*)(void) function pointer member f2.
 * No parameters, no return value.  Side effect verified by checking
 * g_flag after the call.
 */
__attribute__((noinline))
void set_flag(void) {
    g_flag = 1;
}

/*
 * identity_str: returns the input string pointer unchanged.
 *
 * Used as the target of the char *(*)(const char *) function pointer
 * member f3.  One pointer parameter in GP register:
 *   x86-64:  rdi
 *   AArch64: x0
 *   RISC-V:  a0
 *   i686:    stack (cdecl)
 * Return value (pointer) in rax / x0 / a0 / eax.
 */
__attribute__((noinline))
char *identity_str(const char *s) {
    return (char *)s;
}

int main(void) {
    int failures = 0;

    /* Test 1: fptr_size_eq_ptr -- function pointer size == data pointer size */
    if (sizeof(int (*)(int, int)) == sizeof(void *))
        printf("fptr_size_eq_ptr: OK\n");
    else {
        printf("fptr_size_eq_ptr: FAIL (fptr=%zu, ptr=%zu)\n",
               sizeof(int (*)(int, int)), sizeof(void *));
        failures++;
    }

    /* Test 2: sizeof_struct -- struct S size == 3 * sizeof(void*) */
    if (sizeof(struct S) == 3 * sizeof(void *))
        printf("sizeof_struct: OK\n");
    else {
        printf("sizeof_struct: FAIL (struct=%zu, expected=%zu)\n",
               sizeof(struct S), 3 * sizeof(void *));
        failures++;
    }

    /* Test 3: offset_f1 -- f1 at offset 0 */
    {
        struct S s;
        int offset = (int)((char *)&s.f1 - (char *)&s);
        if (offset == 0)
            printf("offset_f1: OK\n");
        else {
            printf("offset_f1: FAIL (offset=%d)\n", offset);
            failures++;
        }
    }

    /* Test 4: offset_f2 -- f2 at offset sizeof(void*) */
    {
        struct S s;
        int offset = (int)((char *)&s.f2 - (char *)&s);
        int expected = (int)sizeof(void *);
        if (offset == expected)
            printf("offset_f2: OK\n");
        else {
            printf("offset_f2: FAIL (offset=%d, expected=%d)\n", offset, expected);
            failures++;
        }
    }

    /* Test 5: offset_f3 -- f3 at offset 2*sizeof(void*) */
    {
        struct S s;
        int offset = (int)((char *)&s.f3 - (char *)&s);
        int expected = 2 * (int)sizeof(void *);
        if (offset == expected)
            printf("offset_f3: OK\n");
        else {
            printf("offset_f3: FAIL (offset=%d, expected=%d)\n", offset, expected);
            failures++;
        }
    }

    /* Test 6: call_f1 -- call add_func through struct member f1 */
    {
        struct S s;
        s.f1 = add_func;
        s.f2 = 0;
        s.f3 = 0;
        if (s.f1(30, 12) == 42)
            printf("call_f1: OK\n");
        else {
            printf("call_f1: FAIL\n");
            failures++;
        }
    }

    /* Test 7: call_f2 -- call set_flag through struct member f2 */
    {
        struct S s;
        g_flag = 0;
        s.f1 = 0;
        s.f2 = set_flag;
        s.f3 = 0;
        s.f2();
        if (g_flag == 1)
            printf("call_f2: OK\n");
        else {
            printf("call_f2: FAIL\n");
            failures++;
        }
    }

    /* Test 8: call_f3 -- call identity_str through struct member f3 */
    {
        struct S s;
        char *result;
        s.f1 = 0;
        s.f2 = 0;
        s.f3 = identity_str;
        result = s.f3("hello");
        if (result[0] == 'h')
            printf("call_f3: OK\n");
        else {
            printf("call_f3: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All struct function pointer member tests passed\n");
    return failures;
}
