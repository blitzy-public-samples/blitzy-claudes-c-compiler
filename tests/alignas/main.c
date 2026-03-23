// Test: C11 _Alignas alignment specifier (ISO/IEC 9899:2011 §6.7.5)
//
// Verifies five aspects of _Alignas:
// 1. _Alignas(N) on struct members raises field alignment to N-byte boundary
// 2. _Alignas(type-name) form resolves type alignment (e.g., _Alignas(int))
// 3. _Alignas(N) on global variables enforces N-byte address alignment
// 4. _Alignas(N) on local variables enforces N-byte stack alignment
// 5. sizeof correctly accounts for _Alignas padding in structs and arrays
//
// This tests the C11 _Alignas keyword specifically.
// The GNU __attribute__((aligned(N))) form is tested in tests/attr_aligned/.
//
// Implementation path:
//   Lexer: token.rs tokenizes _Alignas as TokenKind::Alignas
//   Parser: types.rs:170-175 dispatches to parse_alignas_argument()
//   parse.rs:1138-1170 handles _Alignas(type) and _Alignas(expr) forms
//   AST: ast.rs:212 Declaration.alignment, ast.rs:546 StructFieldDecl.alignment
//   Layout: types.rs:342-344 effective alignment = natural_align.max(explicit)

extern int printf(const char *fmt, ...);

// Scenario 1: _Alignas(16) on struct member with constant expression.
// Natural int alignment is 4; _Alignas(16) raises it to 16.
// Layout: a(1) @ offset 0, pad(15), b(4) @ offset 16
// max_align = 16, sizeof = align_up(20, 16) = 32
struct s1 {
    char a;
    _Alignas(16) int b;
};

// Scenario 2: _Alignas(type-name) form on struct member.
// _Alignas(int) resolves to alignment of int = 4.
// Layout: a(1) @ offset 0, pad(3), b(1) @ offset 4
// max_align = 4, sizeof = align_up(5, 4) = 8
struct s2 {
    char a;
    _Alignas(int) char b;
};

// Scenario 3: Larger _Alignas(32) on char member.
// Layout: a(1) @ offset 0, pad(31), b(1) @ offset 32
// max_align = 32, sizeof = align_up(33, 32) = 64
struct s3 {
    char a;
    _Alignas(32) char b;
};

// Scenario 4: _Alignas(16) on a global variable.
// The linker must place this at a 16-byte-aligned address.
_Alignas(16) int g_aligned = 42;

int main(void) {
    // Test 1: sizeof with _Alignas struct members
    printf("%d\n", (int)sizeof(struct s1));    /* Expected: 32 */
    printf("%d\n", (int)sizeof(struct s2));    /* Expected: 8  */
    printf("%d\n", (int)sizeof(struct s3));    /* Expected: 64 */

    // Test 2: Global variable alignment enforcement
    printf("%d\n", (int)((unsigned long)&g_aligned % 16));  /* Expected: 0 */
    printf("%d\n", g_aligned);                /* Expected: 42 */

    // Test 3: Local variable with _Alignas(32) — stack alignment
    _Alignas(32) char buf[4];
    buf[0] = 'A';
    printf("%d\n", (int)((unsigned long)&buf[0] % 32));  /* Expected: 0 */

    // Test 4: Array of aligned struct has correct stride
    struct s1 arr[2];
    printf("%d\n", (int)sizeof(arr));          /* Expected: 64 (2 * 32) */

    printf("ok\n");
    return 0;
}
