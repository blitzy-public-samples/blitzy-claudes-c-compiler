// Test: #pragma once device+inode deduplication
//
// Compile: ccc -o test main.c
//
// This test verifies that #pragma once prevents duplicate inclusion.
// header.h contains a function definition guarded by #pragma once.
// Including it twice would cause a duplicate symbol error if the
// pragma once mechanism failed. If it works correctly, the second
// include is silently skipped.

#include "header.h"
#include "header.h"

int printf(const char *fmt, ...);

int main(void) {
    printf("%d\n", get_value());
    return 0;
}
