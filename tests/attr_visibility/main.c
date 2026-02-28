// Test: __attribute__((visibility(...))) on functions and variables
//
// Verifies that:
// 1. __attribute__((visibility("default"))) is parsed on functions
// 2. __attribute__((visibility("hidden"))) is parsed on functions
// 3. __attribute__((visibility("protected"))) is parsed on functions
// 4. __attribute__((visibility("default"))) is parsed on variables
// 5. __attribute__((visibility("hidden"))) is parsed on variables
// 6. __attribute__((__visibility__("hidden"))) underscore form is accepted
// 7. All visibility-annotated functions are callable and return correct values
// 8. All visibility-annotated variables are accessible with correct values
//
// Implementation path:
//   Parser: parse.rs:736-738 matches "visibility"|"__visibility__", calls parse_string_attr_arg()
//   AST: ast.rs:34 (DeclAttributes.visibility), ast.rs:356 (FuncAttributes.visibility)
//   IR: module.rs:57-58 (IrFunction.visibility), module.rs:186-187 (IrGlobal.visibility)
//   Backend: elf_writer_common.rs:1244-1252 maps to STV_DEFAULT/STV_HIDDEN/STV_PROTECTED/STV_INTERNAL
//   ELF: constants.rs:93-96 defines STV_DEFAULT=0, STV_INTERNAL=1, STV_HIDDEN=2, STV_PROTECTED=3

int printf(const char *fmt, ...);

// Scenario 1: visibility("default") on function — STV_DEFAULT
__attribute__((visibility("default")))
int default_func(void) {
    return 42;
}

// Scenario 2: visibility("hidden") on function — STV_HIDDEN
__attribute__((visibility("hidden")))
int hidden_func(void) {
    return 99;
}

// Scenario 3: visibility("protected") on function — STV_PROTECTED
__attribute__((visibility("protected")))
int protected_func(void) {
    return 77;
}

// Scenario 4: visibility("default") on global variable
__attribute__((visibility("default")))
int visible_var = 10;

// Scenario 5: visibility("hidden") on global variable
__attribute__((visibility("hidden")))
int hidden_var = 20;

// Scenario 6: __visibility__ underscore form on function
__attribute__((__visibility__("hidden")))
int underscore_func(void) {
    return 55;
}

int main(void) {
    // Call all visibility-annotated functions
    printf("%d\n", default_func());     // expect: 42
    printf("%d\n", hidden_func());      // expect: 99
    printf("%d\n", protected_func());   // expect: 77

    // Access visibility-annotated variables
    printf("%d\n", visible_var);        // expect: 10
    printf("%d\n", hidden_var);         // expect: 20

    // Call underscore-form visibility function
    printf("%d\n", underscore_func());  // expect: 55

    return 0;
}
