// Test: __attribute__((destructor)) function attribute
//
// Verifies that:
// 1. __attribute__((destructor)) is parsed and accepted on functions
// 2. __attribute__((__destructor__)) underscore form is accepted
// 3. __attribute__((destructor(101))) priority form is accepted
// 4. Destructor functions are placed in .fini_array ELF section
// 5. Destructors are called after main() returns
// 6. Destructors execute in reverse declaration order (.fini_array is LIFO)
//
// __attribute__((destructor)) semantics (GCC extension):
// - Function is called after main() returns (or exit() is called)
// - Placed in .fini_array ELF section
// - .fini_array entries are processed in reverse order by the C runtime
// - Optional priority argument: lower number = runs later in destruction

int printf(const char *fmt, ...);

// Scenario 1: Basic destructor — declared first, executed last (reverse order)
__attribute__((destructor))
void cleanup_first(void) {
    printf("cleanup_first\n");
}

// Scenario 2: Second destructor — executed second
__attribute__((destructor))
void cleanup_second(void) {
    printf("cleanup_second\n");
}

// Scenario 3: __destructor__ underscore form — declared third, executed first
__attribute__((__destructor__))
void cleanup_third(void) {
    printf("cleanup_third\n");
}

// Scenario 4: Priority form — verifies priority syntax is accepted
__attribute__((destructor(101)))
void cleanup_prio(void) {
    printf("cleanup_prio\n");
}

// Scenario 5: main — runs first, before any destructor
int main(void) {
    int x = 6 * 7;
    printf("%d\n", x);
    return 0;
}
