// Test: __attribute__((aligned(N))) on structs, struct members, and variables
//
// Verifies that:
// 1. __attribute__((aligned(N))) on a struct raises its alignment to N
// 2. __attribute__((aligned(N))) on a struct member forces that member to N-byte boundary
// 3. __attribute__((__aligned__(N))) underscore form is accepted
// 4. sizeof correctly accounts for alignment padding
// 5. Array stride respects aligned struct size
// 6. Global variable is placed at the specified alignment boundary
//
// Implementation path:
//   Parser: parse.rs:718-722 matches "aligned"|"__aligned__", calls parse_alignment_expr()
//   AST: ast.rs:212 (Declaration.alignment), ast.rs:546 (StructFieldDecl.alignment)
//   Type layout: types.rs:342-344 field alignment = natural_align.max(explicit)
//   Struct-level: types_ctype.rs:308-313 struct_aligned override

int printf(const char *fmt, ...);

// Scenario 1: Struct-level aligned(16)
// Natural layout: char(1) + pad(3) + int(4) = 8 bytes, align 4
// aligned(16) raises alignment to 16, sizeof = align_up(8, 16) = 16
struct s1 {
    char a;
    int b;
} __attribute__((aligned(16)));

// Scenario 2: Per-member aligned(8)
// a(1) at 0, b(1) at 8 (7 bytes padding), c(1) at 9
// max_align = 8, sizeof = align_up(10, 8) = 16
struct s2 {
    char a;
    char b __attribute__((aligned(8)));
    char c;
};

// Scenario 3: __aligned__ underscore form, aligned(32)
// x(1) at 0, struct alignment = 32, sizeof = align_up(1, 32) = 32
struct s3 {
    char x;
} __attribute__((__aligned__(32)));

// Scenario 4: Per-member aligned(16) on int field
// a(1) at 0, b(4) at 16 (15 bytes padding), max_align = 16
// sizeof = align_up(20, 16) = 32
struct s4 {
    char a;
    int b __attribute__((aligned(16)));
};

// Scenario 6: Global variable with aligned(64)
int aligned_var __attribute__((aligned(64))) = 42;

int main(void) {
    // Verify sizeof for aligned structs
    printf("%d\n", (int)sizeof(struct s1));    // 16: struct-level aligned(16)
    printf("%d\n", (int)sizeof(struct s2));    // 16: per-member aligned(8)
    printf("%d\n", (int)sizeof(struct s3));    // 32: __aligned__(32) underscore form
    printf("%d\n", (int)sizeof(struct s4));    // 32: per-member aligned(16)

    // Verify array of aligned struct has correct stride
    struct s1 arr[2];
    printf("%d\n", (int)sizeof(arr));          // 32: 2 * 16

    // Verify global variable address alignment
    printf("%d\n", (int)((unsigned long)&aligned_var % 64));  // 0: 64-byte aligned

    // Verify global variable value (proves correct placement and initialization)
    printf("%d\n", aligned_var);              // 42

    return 0;
}
