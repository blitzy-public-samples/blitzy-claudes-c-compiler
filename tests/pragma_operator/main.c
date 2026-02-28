// Test: _Pragma("...") operator desugaring (C11 §6.10.9)
//
// Compile: ccc -o test main.c
//
// This test verifies that the _Pragma operator correctly desugars its
// string literal argument into an equivalent #pragma directive. The
// _Pragma operator is particularly useful inside macros because #pragma
// cannot appear in a macro definition body.
//
// Features tested:
//   1. _Pragma("once") - equivalent to #pragma once
//   2. _Pragma("pack(push,1)") - equivalent to #pragma pack(push,1)
//   3. _Pragma("pack(pop)") - equivalent to #pragma pack(pop)
//   4. _Pragma inside a macro definition
//   5. String unescaping during destringification

// Test 1: _Pragma("once") at top-level
// This is equivalent to #pragma once — marks this translation unit.
// Since this is the main file (not an included header), it has no
// observable effect on output, but it must be accepted without error.
_Pragma("once")

int printf(const char *fmt, ...);

// A macro that wraps _Pragma to verify it works inside macro bodies.
// This is the primary use case for _Pragma — since #pragma cannot
// appear inside a #define, _Pragma provides the equivalent capability.
#define BEGIN_PACKED _Pragma("pack(push,1)")
#define END_PACKED   _Pragma("pack(pop)")

// Struct with default alignment (for reference measurement)
struct default_aligned {
    char  a;    // offset 0, size 1
    int   b;    // offset 4 (padded to 4-byte alignment), size 4
    char  c;    // offset 8, size 1
    // total: 12 bytes with typical 4-byte alignment padding
};

// Test 2 & 4: _Pragma("pack(push,1)") inside macro + _Pragma("pack(pop)")
// BEGIN_PACKED expands to _Pragma("pack(push,1)") which desugars to
// #pragma pack(push,1), setting 1-byte packing alignment.
BEGIN_PACKED
struct packed_via_pragma {
    char  a;    // offset 0, size 1
    int   b;    // offset 1 (no padding with pack(1)), size 4
    char  c;    // offset 5, size 1
    // total: 6 bytes with 1-byte packing
};
END_PACKED

// Test 3: Direct _Pragma("pack(push,1)") without macro wrapper
_Pragma("pack(push,1)")
struct packed_direct {
    char  x;    // offset 0, size 1
    int   y;    // offset 1 (no padding with pack(1)), size 4
    short z;    // offset 5, size 2
    // total: 7 bytes with 1-byte packing
};
_Pragma("pack(pop)")

int main(void) {
    // Verify default alignment: sizeof should be 12
    // Layout: char(1) + pad(3) + int(4) + char(1) + pad(3) = 12
    int default_size = sizeof(struct default_aligned);

    // Verify packed via macro _Pragma: sizeof should be 6
    // Layout: char(1) + int(4) + char(1) = 6 (no padding)
    int packed_macro_size = sizeof(struct packed_via_pragma);

    // Verify packed via direct _Pragma: sizeof should be 7
    // Layout: char(1) + int(4) + short(2) = 7 (no padding)
    int packed_direct_size = sizeof(struct packed_direct);

    // Print sizes to verify _Pragma desugaring worked correctly
    printf("%d %d %d\n", default_size, packed_macro_size, packed_direct_size);

    return 0;
}
