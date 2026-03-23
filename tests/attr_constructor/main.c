// Test: __attribute__((constructor)) function attribute
//
// Verifies that:
// 1. __attribute__((constructor)) is parsed and accepted on functions
// 2. __attribute__((__constructor__)) underscore form is accepted
// 3. __attribute__((constructor(101))) priority form is accepted
// 4. Constructor functions are placed in .init_array ELF section
// 5. Constructors are called before main() starts
// 6. Constructors execute in declaration order (.init_array is forward-processed)
//
// __attribute__((constructor)) semantics (GCC extension):
// - Function is called before main() starts executing
// - Placed in .init_array ELF section
// - .init_array entries are processed in forward order by the C runtime
// - Optional priority argument: lower number = runs earlier

int printf(const char *fmt, ...);

// Scenario 1: Basic constructor — declared first, executed first (forward order)
__attribute__((constructor))
void init_first(void) {
    printf("init_first\n");
}

// Scenario 2: Second constructor — executed second
__attribute__((constructor))
void init_second(void) {
    printf("init_second\n");
}

// Scenario 3: __constructor__ underscore form — declared third, executed third
__attribute__((__constructor__))
void init_third(void) {
    printf("init_third\n");
}

// Scenario 4: Priority form — verifies priority syntax is accepted
__attribute__((constructor(101)))
void init_prio(void) {
    printf("init_prio\n");
}

// Scenario 5: main — runs after all constructors
int main(void) {
    int x = 6 * 7;
    printf("%d\n", x);
    return 0;
}
