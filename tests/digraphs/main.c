// Test: digraph token compilation
//
// Compile: ccc -o test main.c
//
// No special flags needed — digraphs are recognized unconditionally.
//
// This test exercises all 6 C11 digraph tokens (C11 §6.4.6):
//   <:   -> [   (left bracket)
//   :>   -> ]   (right bracket)
//   <%   -> {   (left brace)
//   %>   -> }   (right brace)
//   %:   -> #   (number sign / preprocessor directive)
//   %:%: -> ##  (token paste / double number sign)
//
// Digraphs are alternate token spellings recognized at the lexer level.
// Unlike trigraphs, they do not require a special flag.

// Digraph 5: %: -> # (preprocessor directive character)
// After lexer mapping: #define DIG_VALUE 100
%:define DIG_VALUE 100

// Digraph 6: %:%: -> ## (token paste operator)
// After lexer mapping: #define PASTE(a, b) a ## b
%:define PASTE(a, b) a %:%: b

int printf(const char *fmt, ...);

// Digraphs 3,4: <% -> { and %> -> } (function body braces)
// After lexer mapping: int main(void) { ... }
int main(void) <%
    // Digraphs 1,2: <: -> [ and :> -> ] (array subscript/declaration)
    // After lexer mapping: int arr[3] = {10, 20, 30};
    int arr<:3:> = <%10, 20, 30%>;

    // Use PASTE macro which uses %:%: (##) for token pasting
    // PASTE(1, 00) -> 1 ## 00 -> 100
    int pasted = PASTE(1, 00);

    // Print all values to verify correct digraph processing:
    //   DIG_VALUE = 100 (from %:define)
    //   arr[0] = 10, arr[1] = 20, arr[2] = 30 (from <:/:>/<%/%>)
    //   pasted = 100 (from %:%: token paste)
    printf("%d %d %d %d %d\n",
           DIG_VALUE, arr<:0:>, arr<:1:>, arr<:2:>, pasted);

    return 0;
%>
