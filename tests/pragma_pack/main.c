// Test: #pragma pack push/pop/reset stack semantics
//
// Compile: ccc -o test main.c
//
// Verifies that:
//   1. #pragma pack(push, N) pushes current alignment and sets new one
//   2. #pragma pack(pop) restores the previously pushed alignment
//   3. #pragma pack() resets to default alignment WITHOUT popping the stack
//   4. Nested push/pop maintains correct stack ordering
//   5. pack() followed by pop() still pops (pack() doesn't clear the stack)

int printf(const char *fmt, ...);

// Test 1: Default alignment (baseline)
// Layout: char(1) + pad(3) + int(4) + char(1) + pad(3) = 12
struct s_default {
    char a;
    int b;
    char c;
};

// Test 2: #pragma pack(push, 1) - push default onto stack, set pack to 1
#pragma pack(push, 1)
// Stack: [None], current = Some(1)
struct s_pack1 {
    char a;
    int b;
    char c;
};
// Layout: char(1) + int(4) + char(1) = 6 (no padding)

// Test 3: Nested #pragma pack(push, 2) - push pack(1) onto stack, set pack to 2
#pragma pack(push, 2)
// Stack: [None, Some(1)], current = Some(2)
struct s_pack2 {
    char a;
    int b;
    char c;
};
// Layout: char(1) + pad(1) + int(4) + char(1) + pad(1) = 8

// Test 4: #pragma pack(pop) - restores pack(1) from stack
#pragma pack(pop)
// Stack: [None], current = Some(1)
struct s_pop_to_1 {
    char a;
    int b;
    char c;
};
// Layout: char(1) + int(4) + char(1) = 6 (pack(1) restored)

// Test 5: #pragma pack(pop) - restores default from stack
#pragma pack(pop)
// Stack: [], current = None (default)
struct s_pop_to_default {
    char a;
    int b;
    char c;
};
// Layout: char(1) + pad(3) + int(4) + char(1) + pad(3) = 12 (default restored)

// Tests 6-9: Verify #pragma pack() reset does NOT pop the stack
#pragma pack(push, 2)
// Stack: [None], current = Some(2)
#pragma pack(push, 1)
// Stack: [None, Some(2)], current = Some(1)

// Test 6: Current is pack(1)
struct s_nested_pack1 {
    char a;
    int b;
    char c;
};
// sizeof = 6

// Test 7: #pragma pack() resets to default but does NOT pop the stack
#pragma pack()
// Stack: [None, Some(2)], current = None (default)
struct s_after_reset {
    char a;
    int b;
    char c;
};
// sizeof = 12 (default)

// Test 8: #pragma pack(pop) pops Some(2) from stack
#pragma pack(pop)
// Stack: [None], current = Some(2)
struct s_pop_after_reset {
    char a;
    int b;
    char c;
};
// sizeof = 8 (pack(2) restored from stack - proves pack() didn't pop)

// Test 9: #pragma pack(pop) pops None (default) from stack
#pragma pack(pop)
// Stack: [], current = None (default)
struct s_pop_final {
    char a;
    int b;
    char c;
};
// sizeof = 12 (default restored)

int main(void) {
    printf("%d\n", (int)sizeof(struct s_default));         // 12
    printf("%d\n", (int)sizeof(struct s_pack1));            // 6
    printf("%d\n", (int)sizeof(struct s_pack2));            // 8
    printf("%d\n", (int)sizeof(struct s_pop_to_1));         // 6
    printf("%d\n", (int)sizeof(struct s_pop_to_default));   // 12
    printf("%d\n", (int)sizeof(struct s_nested_pack1));     // 6
    printf("%d\n", (int)sizeof(struct s_after_reset));      // 12
    printf("%d\n", (int)sizeof(struct s_pop_after_reset));  // 8
    printf("%d\n", (int)sizeof(struct s_pop_final));        // 12
    return 0;
}
