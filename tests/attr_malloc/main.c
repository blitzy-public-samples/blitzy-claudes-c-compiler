// Test: __attribute__((malloc)) function attribute
//
// Verifies that:
// 1. __attribute__((malloc)) is parsed and accepted on functions
// 2. __attribute__((__malloc__)) underscore form is accepted
// 3. Functions with malloc attribute return pointers that work correctly
// 4. Returned pointers from malloc-attributed functions do not alias each other
// 5. Attributed and non-attributed allocator functions coexist correctly
//
// __attribute__((malloc)) semantics (GCC extension):
// - Function returns a pointer that does NOT alias any other existing pointer
// - No other pointer to the same memory block exists when the function returns
// - Enables optimizer alias analysis assumptions about pointer uniqueness
// - Does not change visible behavior — purely an optimization hint
// - Common usage: malloc(), calloc(), custom allocator wrappers

int printf(const char *fmt, ...);
void *malloc(unsigned long size);
void free(void *ptr);

// Scenario 1: basic __attribute__((malloc)) on custom allocator
__attribute__((malloc))
int *create_int(int val) {
    int *p = (int *)malloc(sizeof(int));
    *p = val;
    return p;
}

// Scenario 2: __malloc__ underscore form — GNU alternate spelling
__attribute__((__malloc__))
int *create_sum(int a, int b) {
    int *p = (int *)malloc(sizeof(int));
    *p = a + b;
    return p;
}

// Scenario 3: malloc attribute with product computation
__attribute__((malloc))
int *create_product(int a, int b) {
    int *p = (int *)malloc(sizeof(int));
    *p = a * b;
    return p;
}

// Scenario 4: Normal function — no attribute, baseline comparison
int *plain_create(int val) {
    int *p = (int *)malloc(sizeof(int));
    *p = val;
    return p;
}

int main(void) {
    int *a = create_int(42);          // allocate, store 42
    int *b = create_sum(17, 25);      // allocate, store 17 + 25 = 42
    int *c = create_product(6, 7);    // allocate, store 6 * 7 = 42
    int *d = plain_create(99);        // allocate, store 99

    // Verify non-aliasing: all malloc-attributed + plain allocations return distinct pointers
    int distinct = (a != b) && (a != c) && (a != d) && (b != c) && (b != d) && (c != d);

    printf("%d %d %d %d %d\n", *a, *b, *c, *d, distinct);

    free(a);
    free(b);
    free(c);
    free(d);
    return 0;
}
