// Test: parser multi-error recovery
//
// This file contains 5 intentional syntax errors. The compiler must:
// 1. Report ALL 5 errors (not stop at the first one)
// 2. Include source-line-and-caret output for each error
// 3. Use synchronization-based recovery to continue after each error
// 4. Exit with code 1 (compilation failure)
//
// The 5 errors test different recovery scenarios:
//   Error 1: Missing semicolon after declaration
//   Error 2: Missing expression in brace-enclosed initializer
//   Error 3: Missing closing parenthesis in grouped expression
//   Error 4: Duplicate type specifier ('double double')
//   Error 5: Missing closing parenthesis in function parameter list

// Error 1: Missing semicolon after global variable declaration.
// Parser should report "expected ';' after declaration" and synchronize
// at the next declaration keyword ('int' on the error2 line).
int error1 = 10

// Error 2: Missing expression inside brace-enclosed initializer.
// The '{' starts a brace initializer but ';' appears where an expression
// is expected. Parser should report "expected expression before ';'"
// and synchronize at the ';' inside the braces.
int error2 = {;};

// Error 3: Missing closing parenthesis in grouped expression.
// Parser should report "expected ')' before ';'" with a
// "to match this '('" note, then synchronize at ';'.
int error3 = (100 + 200 ;

// Error 4: Duplicate type specifier — 'double double' is invalid.
// Parser sees 'double' as a complete type, then encounters another
// 'double' where a declarator is expected. It reports "expected ';'
// after declaration" and then parses 'double error4 = 1.0;' cleanly.
double double error4 = 1.0;

// Error 5: Missing closing parenthesis in function parameter list.
// Parser should report "expected ')' before '{'" with a
// "to match this '('" note. The '{' begins a function body,
// so the parser recovers by treating it as a function definition.
int error5(void {
    return 0;
}
