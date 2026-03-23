/* Regression test for RISC-V dash shell compilation bug.
 * Exercises dash-like patterns: union-based node types, function pointer
 * dispatch tables, switch-case fallthrough, goto, volatile state variables,
 * and string scanning. These patterns stress RISC-V codegen paths that
 * caused dash POSIX shell compilation to fail on riscv while passing on
 * x86/i686/arm.
 *
 * Expected output: 414
 * Expected return: 0
 */

int printf(const char *fmt, ...);

#define NCMD   0
#define NPIPE  1
#define NAND   2
#define NOR    3
#define NSEMI  4

typedef int (*builtin_fn)(int);

struct ncmd {
    int type;
    int argc;
};

struct npipe {
    int type;
    int count;
    int cmds[4];
};

struct nbinary {
    int type;
    int left_val;
    int right_val;
};

union node {
    int type;
    struct ncmd cmd;
    struct npipe pipe;
    struct nbinary binary;
};

static int cmd_echo(int val) {
    return val;
}

static int cmd_true(int val) {
    (void)val;
    return 0;
}

static int cmd_false(int val) {
    (void)val;
    return 1;
}

static builtin_fn builtins[] = { cmd_echo, cmd_true, cmd_false };

static int evaltree(union node *n) {
    int status = -1;

    switch (n->type) {
    case NCMD:
        status = builtins[0](n->cmd.argc);
        break;
    case NPIPE: {
        int total = 0;
        int i;
        for (i = 0; i < n->pipe.count; i++) {
            total += n->pipe.cmds[i];
        }
        status = total;
        break;
    }
    case NAND: {
        int left = n->binary.left_val;
        if (left == 0)
            status = n->binary.right_val;
        else
            status = left;
        break;
    }
    case NOR: {
        int left = n->binary.left_val;
        if (left != 0)
            status = n->binary.right_val;
        else
            status = left;
        break;
    }
    default:
        status = 0;
        break;
    }

    return status;
}

static int test_evaltree(void) {
    union node nodes[4];
    int result = 0;

    /* NCMD: builtins[0](42) = cmd_echo(42) = 42 */
    nodes[0].cmd.type = NCMD;
    nodes[0].cmd.argc = 42;
    result += evaltree(&nodes[0]);

    /* NPIPE: 2 + 1 + 4 = 7 */
    nodes[1].pipe.type = NPIPE;
    nodes[1].pipe.count = 3;
    nodes[1].pipe.cmds[0] = 2;
    nodes[1].pipe.cmds[1] = 1;
    nodes[1].pipe.cmds[2] = 4;
    result += evaltree(&nodes[1]);

    /* NAND: left=0, right=5 -> status=5 */
    nodes[2].binary.type = NAND;
    nodes[2].binary.left_val = 0;
    nodes[2].binary.right_val = 5;
    result += evaltree(&nodes[2]);

    /* NOR: left=1 (!=0), right=8 -> status=8 */
    nodes[3].binary.type = NOR;
    nodes[3].binary.left_val = 1;
    nodes[3].binary.right_val = 8;
    result += evaltree(&nodes[3]);

    return result; /* 42 + 7 + 5 + 8 = 62 */
}

static int test_control_flow(void) {
    volatile int state = 0;
    int result = 0;
    int i;

    for (i = 0; i < 10; i++) {
        switch (i % 4) {
        case 0:
            state = i + 1;
            break;
        case 1:
            result += state;
            /* fallthrough to case 2 */
        case 2:
            result += i;
            break;
        case 3:
            if (state > 5)
                goto done;
            result -= 1;
            break;
        }
    }
done:
    return result; /* 36 */
}

static int test_string_ops(void) {
    const char *input = "echo hello world";
    const char *p = input;
    int word_count = 1;
    int length = 0;

    while (*p) {
        if (*p == ' ')
            word_count++;
        length++;
        p++;
    }

    return word_count * 100 + length; /* 3 * 100 + 16 = 316 */
}

int main(void) {
    int total = 0;

    total += test_evaltree();      /* 62 */
    total += test_control_flow();   /* 36 */
    total += test_string_ops();     /* 316 */

    printf("%d\n", total);          /* 414 */

    return 0;
}
