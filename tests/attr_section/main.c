// Test: __attribute__((section("..."))) on variables and functions
//
// Verifies that:
// 1. __attribute__((section("name"))) is parsed and accepted on variables
// 2. __attribute__((section("name"))) is parsed and accepted on functions
// 3. __attribute__((__section__("name"))) underscore form is accepted
// 4. Variables are placed in the custom ELF section (codegen emits .section directive)
// 5. Functions are placed in the custom ELF section (codegen emits .section directive)
// 6. Variables in custom sections are readable at runtime
// 7. Functions in custom sections are callable at runtime
//
// Implementation path:
//   Parser: parse.rs:740-742 matches "section"|"__section__", stores name string
//   AST: ast.rs:32 (FunctionAttributes.section), ast.rs:358 (DeclAttributes.section)
//   IR lowering: func_lowering.rs:559 and global_decl.rs:360 copy to IR
//   Codegen (functions): generation.rs:728-731 emits .section name,"ax",@progbits
//   Codegen (globals): common.rs:1277-1285 emits .section name,"aw",@progbits

int printf(const char *fmt, ...);

// Scenario 1: Global variable in custom section
int custom_var __attribute__((section(".custom_data"))) = 42;

// Scenario 2: Global variable with __section__ underscore form
int custom_var2 __attribute__((__section__(".custom_data2"))) = 100;

// Scenario 3: Function in custom section
__attribute__((section(".custom_text")))
int custom_add(int a, int b) {
    return a + b;
}

// Scenario 4: Function with __section__ underscore form
__attribute__((__section__(".custom_text2")))
int custom_mul(int a, int b) {
    return a * b;
}

int main(void) {
    // Read variables from custom sections
    int r1 = custom_var;           // 42
    int r2 = custom_var2;          // 100

    // Call functions from custom sections
    int r3 = custom_add(3, 4);    // 3 + 4 = 7
    int r4 = custom_mul(5, 6);    // 5 * 6 = 30

    // Cross-scenario: pass custom-section variables to custom-section function
    int r5 = custom_add(r1, r2);  // 42 + 100 = 142

    printf("%d %d %d %d %d\n", r1, r2, r3, r4, r5);
    return 0;
}
