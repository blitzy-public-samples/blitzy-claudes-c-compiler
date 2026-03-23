// Test: trigraph processing with -trigraphs flag
//
// Compile: ccc -trigraphs -o test main.c
//
// This test exercises all 9 ISO C trigraph sequences (C11 5.2.1.1).
// Trigraph replacement occurs in Phase 1 of translation, before line
// splicing and preprocessing. The -trigraphs flag must be passed to
// activate this phase.
//
// Trigraph sequences tested:
//   ??= -> #   (preprocessor directive)
//   ??( -> [   (left bracket)
//   ??) -> ]   (right bracket)
//   ??< -> {   (left brace)
//   ??> -> }   (right brace)
//   ??/ -> \   (backslash / line continuation)
//   ??' -> ^   (caret / XOR)
//   ??! -> |   (pipe / OR)
//   ??- -> ~   (tilde / bitwise NOT)

// Trigraph 1: ??= -> # (used as preprocessor directive character)
// After Phase 1 replacement, this line becomes: #define TRIG_VALUE 100
??=define TRIG_VALUE 100

int printf(const char *fmt, ...);

// Trigraph 4,5: ??< -> { and ??> -> } (used as function body braces)
// After Phase 1 replacement: int main(void) { ... }
int main(void) ??<
    // Trigraph 2,3: ??( -> [ and ??) -> ] (used for array declaration)
    // After replacement: int arr[3] = {10, 20, 30};
    int arr??(3??) = ??<10, 20, 30??>;

    // Trigraph 7: ??' -> ^ (XOR operator)
    // After replacement: int xor_val = 5 ^ 3;
    // 5 = 0b101, 3 = 0b011, XOR = 0b110 = 6
    int xor_val = 5 ??' 3;

    // Trigraph 8: ??! -> | (OR operator)
    // After replacement: int or_val = 5 | 3;
    // 5 = 0b101, 3 = 0b011, OR = 0b111 = 7
    int or_val = 5 ??! 3;

    // Trigraph 9: ??- -> ~ (bitwise NOT)
    // After replacement: int not_val = ~0;
    // ~0 = 0xFFFFFFFF = -1 (two's complement, 32-bit int)
    int not_val = ??-0;

    // Trigraph 6: ??/ -> \ (line continuation)
    // After Phase 1: int line\ (newline) _cont = 42;
    // After Phase 2 (line splicing): int line_cont = 42;
    int line??/
_cont = 42;

    // Print all values to verify correct trigraph processing:
    //   TRIG_VALUE = 100 (from ??=define)
    //   arr[0] = 10, arr[1] = 20, arr[2] = 30 (from ??(/??))
    //   xor_val = 6 (from ??')
    //   or_val = 7 (from ??!)
    //   not_val = -1 (from ??-)
    //   line_cont = 42 (from ??/)
    printf("%d %d %d %d %d %d %d %d\n",
           TRIG_VALUE, arr??(0??), arr??(1??), arr??(2??),
           xor_val, or_val, not_val, line_cont);

    return 0;
??>
