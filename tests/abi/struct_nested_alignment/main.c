/*
 * ABI integration test: Nested struct alignment propagation.
 *
 * Verifies that CCC correctly propagates alignment from a nested struct's
 * member to the outer struct's layout. The core invariant: an inner struct
 * containing a `double` member forces 8-byte alignment in the outer struct
 * on 64-bit targets, resulting in padding between `char a` and `struct Inner b`.
 *
 * Architecture-dependent layout:
 *   64-bit (x86-64, AArch64, RISC-V 64): alignof(double) = 8
 *     Inner:  sizeof=8, alignof=8
 *     Outer:  offsetof(b)=8, sizeof=16, alignof=8
 *
 *   i686:  alignof(double) = 4
 *     Inner:  sizeof=8, alignof=4
 *     Outer:  offsetof(b)=4, sizeof=12, alignof=4
 *
 * Exercises: src/frontend/sema/analysis.rs (StructLayoutBuilder)
 *            src/common/types.rs (type alignment computation)
 */
#include <stdio.h>
#include <stddef.h>

struct Inner { double x; };
struct Outer { char a; struct Inner b; };

__attribute__((noinline))
struct Outer make_outer(char a, double x) {
    struct Outer o;
    o.a = a;
    o.b.x = x;
    return o;
}

int main(void) {
    int failures = 0;
    int is_64bit = (sizeof(void*) == 8);

    /* Test 1: sizeof(struct Inner) == 8 (architecture-independent) */
    if (sizeof(struct Inner) == 8)
        printf("sizeof_Inner: OK\n");
    else {
        printf("sizeof_Inner: FAIL (got %d)\n", (int)sizeof(struct Inner));
        failures++;
    }

    /* Test 2: _Alignof(struct Inner) — 8 on 64-bit, 4 on i686 */
    {
        int expected = is_64bit ? 8 : 4;
        if ((int)_Alignof(struct Inner) == expected)
            printf("alignof_Inner: OK\n");
        else {
            printf("alignof_Inner: FAIL (got %d)\n", (int)_Alignof(struct Inner));
            failures++;
        }
    }

    /* Test 3: offsetof(struct Outer, a) == 0 (architecture-independent) */
    if (offsetof(struct Outer, a) == 0)
        printf("offsetof_a: OK\n");
    else {
        printf("offsetof_a: FAIL (got %d)\n", (int)offsetof(struct Outer, a));
        failures++;
    }

    /* Test 4: offsetof(struct Outer, b) — 8 on 64-bit, 4 on i686 (KEY ASSERTION) */
    {
        int expected = is_64bit ? 8 : 4;
        if ((int)offsetof(struct Outer, b) == expected)
            printf("offsetof_b: OK\n");
        else {
            printf("offsetof_b: FAIL (got %d)\n", (int)offsetof(struct Outer, b));
            failures++;
        }
    }

    /* Test 5: sizeof(struct Outer) — 16 on 64-bit, 12 on i686 (KEY ASSERTION) */
    {
        int expected = is_64bit ? 16 : 12;
        if ((int)sizeof(struct Outer) == expected)
            printf("sizeof_Outer: OK\n");
        else {
            printf("sizeof_Outer: FAIL (got %d)\n", (int)sizeof(struct Outer));
            failures++;
        }
    }

    /* Test 6: _Alignof(struct Outer) — 8 on 64-bit, 4 on i686 */
    {
        int expected = is_64bit ? 8 : 4;
        if ((int)_Alignof(struct Outer) == expected)
            printf("alignof_Outer: OK\n");
        else {
            printf("alignof_Outer: FAIL (got %d)\n", (int)_Alignof(struct Outer));
            failures++;
        }
    }

    /* Test 7: Value correctness through nested struct via noinline return */
    {
        struct Outer o = make_outer('Z', 2.718);
        if (o.a == 'Z' && o.b.x == 2.718)
            printf("value_nested: OK\n");
        else {
            printf("value_nested: FAIL\n");
            failures++;
        }
    }

    /* Test 8: Double precision preserved through nesting */
    {
        struct Outer o;
        o.a = 'A';
        o.b.x = 1.23456789012345;
        if (o.b.x == 1.23456789012345)
            printf("double_precision: OK\n");
        else {
            printf("double_precision: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All nested alignment tests passed\n");
    return failures;
}
