// Test: #pragma once device+inode deduplication
//
// Compile: ccc -o test main.c
//
// Verifies that #pragma once correctly deduplicates includes of the
// same header file, preventing duplicate definition errors.
// The header is included through multiple routes:
//   1. Direct: #include "header.h"
//   2. Direct duplicate: #include "header.h" (same path again)
//   3. Transitive: #include "indirect.h" -> #include "header.h"
//
// header.h defines a non-static function get_value(). Without proper
// #pragma once deduplication, the duplicate definitions would cause
// a compilation error.

// First include: processes header.h, records it in #pragma once set
#include "header.h"

// Second include: same file, same path — must be skipped by #pragma once
#include "header.h"

// Third include: indirect.h includes header.h transitively —
// #pragma once must still prevent the duplicate inclusion
#include "indirect.h"

int printf(const char *fmt, ...);

int main(void) {
    // If we reach here, #pragma once deduplication worked correctly —
    // header.h was processed exactly once despite three include directives.
    printf("%d\n", get_value());
    return 0;
}
