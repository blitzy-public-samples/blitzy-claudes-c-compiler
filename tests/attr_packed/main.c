// Test: __attribute__((packed)) struct attribute
//
// Verifies that:
// 1. __attribute__((packed)) removes padding between struct members
// 2. sizeof returns the tightly-packed size (no padding, no trailing alignment)
// 3. __attribute__((__packed__)) underscore form is accepted
// 4. Nested packed structs maintain packing in outer structs
// 5. Member access works correctly on packed structs

int printf(const char *fmt, ...);

// Scenario 1: Struct-level packed — char(1) + int(4) + short(2) = 7 bytes
struct packed1 {
    char a;
    int b;
    short c;
} __attribute__((packed));

// Scenario 2: Non-packed comparison — char(1) + pad(3) + int(4) + short(2) + pad(2) = 12 bytes
struct unpacked {
    char a;
    int b;
    short c;
};

// Scenario 3: __packed__ underscore form — char(1) + int(4) + char(1) + short(2) = 8 bytes
struct packed2 {
    char a;
    int b;
    char c;
    short d;
} __attribute__((__packed__));

// Scenario 4: Nested packed struct
struct inner_packed {
    char x;
    int y;
} __attribute__((packed));

struct outer {
    struct inner_packed inner;
    char z;
};

int main(void) {
    // Verify sizeof for packed vs unpacked structs
    printf("%d\n", (int)sizeof(struct packed1));       // expect: 7
    printf("%d\n", (int)sizeof(struct unpacked));       // expect: 12
    printf("%d\n", (int)sizeof(struct packed2));        // expect: 8
    printf("%d\n", (int)sizeof(struct inner_packed));   // expect: 5
    printf("%d\n", (int)sizeof(struct outer));          // expect: 6

    // Verify member access works correctly on packed struct
    struct packed1 p1;
    p1.a = 1;
    p1.b = 2;
    p1.c = 3;
    printf("%d %d %d\n", p1.a, p1.b, p1.c);           // expect: 1 2 3

    return 0;
}
