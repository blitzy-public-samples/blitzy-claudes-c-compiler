int printf(const char *fmt, ...);

/* At -O0: no constant folding, no copy propagation, no DCE, no inlining.
   All patterns below work correctly even without any optimization passes. */

static int add(int a, int b) {
    return a + b;
}

int main(void) {
    /* Constant expression: const_fold would fold 2+3 at compile time.
       At -O0 this is computed at runtime — still correct. */
    int a = 2 + 3;

    /* Copy chain: copy_prop would propagate a through b to c.
       At -O0 each variable occupies its own stack slot — still correct. */
    int b = a;
    int c = b;

    /* Dead computation: DCE would eliminate this entirely.
       At -O0 it is computed and discarded — still correct. */
    int dead = c * c * c;
    (void)dead;

    /* Function call: inliner would inline add() at -O2+.
       At -O0 the call goes through normal calling convention — still correct. */
    int result = add(c, 10);

    printf("%d\n", result);
    return 0;
}
