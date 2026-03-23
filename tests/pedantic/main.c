// Test: -pedantic GNU extension warning
//
// Compile: ccc -pedantic -o test main.c
//
// This test uses GNU C extensions that are not part of standard C11.
// When compiled with -pedantic, the compiler should emit warnings for
// each non-standard construct, but still accept and compile the code
// correctly. The program verifies runtime correctness of all extensions.
//
// GNU extensions exercised:
//   1. typeof(expr) - type computed from expression
//   2. Statement expressions ({ ... }) - compound statement as expression
//   3. Zero-length arrays in structs - char data[0]
//   4. Case ranges - case low ... high:

int printf(const char *fmt, ...);

// GNU extension: zero-length array in struct
// Standard C11 uses flexible array member: char data[]
// GCC extension allows: char data[0]
// With -pedantic, this should produce a warning about non-standard
// zero-length array usage.
struct tagged_buffer {
    int length;
    char data[0];
};

int main(void) {
    // Verify zero-length array struct layout:
    // sizeof(struct tagged_buffer) should equal sizeof(int) because
    // data[0] contributes zero bytes. This confirms the compiler
    // correctly handles the GNU zero-length array extension.
    // (On all CCC targets, sizeof(int) == 4)
    if (sizeof(struct tagged_buffer) != sizeof(int)) {
        printf("FAIL: sizeof tagged_buffer\n");
        return 1;
    }

    // GNU extension: typeof keyword
    // Not part of C11 (C23 adds typeof but C11 does not have it).
    // With -pedantic, this should produce a warning about non-standard
    // typeof usage.
    int x = 42;
    typeof(x) y = x + 1;  // y has type int, value 43

    // GNU extension: statement expression
    // Allows a compound statement enclosed in ({ ... }) to be used as
    // an expression. The value is the last expression in the block.
    // With -pedantic, this should produce a warning about non-standard
    // statement expression usage.
    int z = ({ int tmp = y * 2; tmp + 1; });  // tmp=86, z=87

    // GNU extension: case range
    // Allows a range of values in a single case label using "..."
    // notation. Standard C requires individual case labels.
    // With -pedantic, this should produce a warning about non-standard
    // case range usage.
    int category;
    switch (z) {
        case 0 ... 50:
            category = 1;
            break;
        case 51 ... 100:
            category = 2;
            break;
        default:
            category = 3;
            break;
    }

    // Print computed values to verify all GNU extensions work correctly:
    //   y = 43  (typeof: int = 42 + 1)
    //   z = 87  (statement expr: { 43*2=86; 86+1=87 })
    //   category = 2  (case range: 87 in [51..100])
    printf("%d %d %d\n", y, z, category);
    return 0;
}
