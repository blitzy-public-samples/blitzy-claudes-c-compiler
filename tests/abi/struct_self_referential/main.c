/*
 * ABI integration test: self-referential struct with pointer-to-self field
 *
 * Verifies that CCC correctly handles the canonical linked-list node pattern:
 *
 *   struct Node { int value; struct Node *next; };
 *
 * This exercises:
 *   1. Struct layout with a pointer member that references its own type.
 *      The compiler must correctly compute sizeof and field offsets for a
 *      struct that contains a pointer to itself (an incomplete type at the
 *      point the field is declared, but pointers always have known size).
 *
 *   2. LP64 vs ILP32 layout differences due to pointer size:
 *      - LP64 (x86-64, AArch64, RISC-V 64):
 *          int value   at offset 0, size 4, align 4
 *          [4 bytes padding to align pointer to 8]
 *          struct Node *next  at offset 8, size 8, align 8
 *          sizeof = 16, alignof = 8
 *
 *      - ILP32 (i686):
 *          int value   at offset 0, size 4, align 4
 *          struct Node *next  at offset 4, size 4, align 4
 *          sizeof = 8, alignof = 4
 *
 *   3. Functional linked-list traversal across noinline function boundaries
 *      to exercise the actual calling convention for pointer-to-struct args.
 *
 *   4. Self-referencing (node.next points to itself) and NULL termination.
 *
 * All helper functions use __attribute__((noinline)) to prevent the optimizer
 * from inlining calls, ensuring the actual calling convention and struct
 * layout ABI is exercised across function boundaries.
 *
 * No architecture-specific preprocessor guards are used.  The conditional
 * logic uses sizeof(void *) to distinguish LP64 from ILP32 at compile time.
 */

#include <stdio.h>

struct Node {
    int value;
    struct Node *next;
};

/*
 * sum_list: traverse a linked list summing all value fields.
 *
 * Receives a pointer to struct Node in the first GP register:
 *   x86-64:  rdi
 *   AArch64: x0
 *   RISC-V:  a0
 *   i686:    stack (cdecl)
 *
 * Returns int sum in the return register (eax / w0 / a0 / eax).
 */
__attribute__((noinline))
int sum_list(struct Node *head) {
    int sum = 0;
    struct Node *cur = head;
    while (cur != 0) {
        sum = sum + cur->value;
        cur = cur->next;
    }
    return sum;
}

/*
 * count_list: traverse a linked list counting nodes.
 *
 * Same calling convention as sum_list — receives pointer to struct Node,
 * returns int count.
 */
__attribute__((noinline))
int count_list(struct Node *head) {
    int count = 0;
    struct Node *cur = head;
    while (cur != 0) {
        count = count + 1;
        cur = cur->next;
    }
    return count;
}

int main(void) {
    int failures = 0;

    /* Test 1: sizeof(struct Node) — LP64: 16, ILP32: 8 */
    {
        int ok;
        if (sizeof(void *) == 8)
            ok = (sizeof(struct Node) == 16);
        else
            ok = (sizeof(struct Node) == 8);
        if (ok)
            printf("sizeof_node: OK\n");
        else {
            printf("sizeof_node: FAIL\n");
            failures++;
        }
    }

    /* Test 2: Offset of next field — LP64: 8, ILP32: 4 */
    {
        struct Node n;
        int offset = (int)((char *)&n.next - (char *)&n);
        int ok;
        if (sizeof(void *) == 8)
            ok = (offset == 8);
        else
            ok = (offset == 4);
        if (ok)
            printf("offset_next: OK\n");
        else {
            printf("offset_next: FAIL\n");
            failures++;
        }
    }

    /* Test 3: Offset of value field — always 0 (first field) */
    {
        struct Node n;
        int offset = (int)((char *)&n.value - (char *)&n);
        if (offset == 0)
            printf("offset_value: OK\n");
        else {
            printf("offset_value: FAIL\n");
            failures++;
        }
    }

    /* Test 4: Linked list sum traversal — 3 nodes: 10+20+30 = 60 */
    {
        struct Node n1, n2, n3;
        n1.value = 10;
        n1.next = &n2;
        n2.value = 20;
        n2.next = &n3;
        n3.value = 30;
        n3.next = 0;
        if (sum_list(&n1) == 60)
            printf("list_sum: OK\n");
        else {
            printf("list_sum: FAIL\n");
            failures++;
        }
    }

    /* Test 5: Linked list count — 3 nodes */
    {
        struct Node n1, n2, n3;
        n1.value = 1;
        n1.next = &n2;
        n2.value = 2;
        n2.next = &n3;
        n3.value = 3;
        n3.next = 0;
        if (count_list(&n1) == 3)
            printf("list_count: OK\n");
        else {
            printf("list_count: FAIL\n");
            failures++;
        }
    }

    /* Test 6: Self-referencing — node.next points to itself */
    {
        struct Node self;
        self.value = 42;
        self.next = &self;
        if (self.next->value == 42 && self.next->next == &self)
            printf("self_ref: OK\n");
        else {
            printf("self_ref: FAIL\n");
            failures++;
        }
    }

    /* Test 7: NULL next pointer — leaf node */
    {
        struct Node leaf;
        leaf.value = 99;
        leaf.next = 0;
        if (leaf.next == 0 && leaf.value == 99)
            printf("null_next: OK\n");
        else {
            printf("null_next: FAIL\n");
            failures++;
        }
    }

    if (failures == 0)
        printf("All self-referential struct tests passed\n");
    return failures;
}
