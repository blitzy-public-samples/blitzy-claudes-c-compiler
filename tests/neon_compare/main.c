#include <arm_neon.h>

int printf(const char *fmt, ...);

/* Test 1: vceqq_u32 — element-wise equality comparison */
static void test_vceqq_u32(void) {
    unsigned int ceq_a[4] = {1, 2, 3, 4};
    unsigned int ceq_b[4] = {1, 5, 3, 7};
    uint32x4_t va = vld1q_u32(ceq_a);
    uint32x4_t vb = vld1q_u32(ceq_b);
    uint32x4_t ceq_res = vceqq_u32(va, vb);
    unsigned int ceq_out[4];
    vst1q_u32(ceq_out, ceq_res);
    printf("vceqq_u32: %08x %08x %08x %08x\n",
           ceq_out[0], ceq_out[1], ceq_out[2], ceq_out[3]);
}

/* Test 2: vcgtq_s16 — signed greater-than comparison */
static void test_vcgtq_s16(void) {
    int16x8_t s16_a, s16_b;
    s16_a.__val[0] = 5;    s16_b.__val[0] = 3;
    s16_a.__val[1] = -3;   s16_b.__val[1] = -5;
    s16_a.__val[2] = 0;    s16_b.__val[2] = 0;
    s16_a.__val[3] = 10;   s16_b.__val[3] = 1;
    s16_a.__val[4] = -1;   s16_b.__val[4] = 0;
    s16_a.__val[5] = 7;    s16_b.__val[5] = -2;
    s16_a.__val[6] = -100; s16_b.__val[6] = -50;
    s16_a.__val[7] = 50;   s16_b.__val[7] = 100;
    uint16x8_t cgt_res = vcgtq_s16(s16_a, s16_b);
    unsigned short cgt_out[8];
    vst1q_u16(cgt_out, cgt_res);
    printf("vcgtq_s16: %04x %04x %04x %04x %04x %04x %04x %04x\n",
           cgt_out[0], cgt_out[1], cgt_out[2], cgt_out[3],
           cgt_out[4], cgt_out[5], cgt_out[6], cgt_out[7]);
}

/* Test 3: vandq_u32 — bitwise AND */
static void test_vandq_u32(void) {
    unsigned int and_a[4] = {0xFF00FF00, 0x0F0F0F0F, 0xAAAAAAAA, 0x12345678};
    unsigned int and_b[4] = {0x0F0F0F0F, 0xF0F0F0F0, 0x55555555, 0xFFFF0000};
    uint32x4_t va_and = vld1q_u32(and_a);
    uint32x4_t vb_and = vld1q_u32(and_b);
    uint32x4_t and_res = vandq_u32(va_and, vb_and);
    unsigned int and_out[4];
    vst1q_u32(and_out, and_res);
    printf("vandq_u32: %08x %08x %08x %08x\n",
           and_out[0], and_out[1], and_out[2], and_out[3]);
}

/* Test 4: vorrq_u32 — bitwise OR */
static void test_vorrq_u32(void) {
    unsigned int and_a[4] = {0xFF00FF00, 0x0F0F0F0F, 0xAAAAAAAA, 0x12345678};
    unsigned int and_b[4] = {0x0F0F0F0F, 0xF0F0F0F0, 0x55555555, 0xFFFF0000};
    uint32x4_t va_and = vld1q_u32(and_a);
    uint32x4_t vb_and = vld1q_u32(and_b);
    uint32x4_t orr_res = vorrq_u32(va_and, vb_and);
    unsigned int orr_out[4];
    vst1q_u32(orr_out, orr_res);
    printf("vorrq_u32: %08x %08x %08x %08x\n",
           orr_out[0], orr_out[1], orr_out[2], orr_out[3]);
}

/* Test 5: veorq_u32 — bitwise XOR */
static void test_veorq_u32(void) {
    unsigned int and_a[4] = {0xFF00FF00, 0x0F0F0F0F, 0xAAAAAAAA, 0x12345678};
    unsigned int and_b[4] = {0x0F0F0F0F, 0xF0F0F0F0, 0x55555555, 0xFFFF0000};
    uint32x4_t va_and = vld1q_u32(and_a);
    uint32x4_t vb_and = vld1q_u32(and_b);
    uint32x4_t eor_res = veorq_u32(va_and, vb_and);
    unsigned int eor_out[4];
    vst1q_u32(eor_out, eor_res);
    printf("veorq_u32: %08x %08x %08x %08x\n",
           eor_out[0], eor_out[1], eor_out[2], eor_out[3]);
}

/* Test 6: vbicq_u8 — bit clear (a & ~b) */
static void test_vbicq_u8(void) {
    unsigned char bic_a[16] = {0xFF,0xFF,0xFF,0xFF, 0xAA,0xAA,0xAA,0xAA,
                               0x55,0x55,0x55,0x55, 0x00,0x00,0x00,0x00};
    unsigned char bic_b[16] = {0x0F,0xF0,0x00,0xFF, 0x0F,0xF0,0x00,0xFF,
                               0x0F,0xF0,0x00,0xFF, 0x0F,0xF0,0x00,0xFF};
    uint8x16_t va_bic = vld1q_u8(bic_a);
    uint8x16_t vb_bic = vld1q_u8(bic_b);
    uint8x16_t bic_res = vbicq_u8(va_bic, vb_bic);
    unsigned char bic_out[16];
    vst1q_u8(bic_out, bic_res);
    printf("vbicq_u8:");
    for (int i = 0; i < 16; i++)
        printf(" %02x", bic_out[i]);
    printf("\n");
}

/* Test 7: vbslq_u32 — bitwise select: (sel & a) | (~sel & b) */
static void test_vbslq_u32(void) {
    unsigned int bsl_sel[4] = {0xFFFFFFFF, 0x00000000, 0xFF00FF00, 0x0F0F0F0F};
    unsigned int bsl_a[4]   = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    unsigned int bsl_b[4]   = {0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD};
    uint32x4_t v_sel  = vld1q_u32(bsl_sel);
    uint32x4_t v_bsla = vld1q_u32(bsl_a);
    uint32x4_t v_bslb = vld1q_u32(bsl_b);
    uint32x4_t bsl_res = vbslq_u32(v_sel, v_bsla, v_bslb);
    unsigned int bsl_out[4];
    vst1q_u32(bsl_out, bsl_res);
    printf("vbslq_u32: %08x %08x %08x %08x\n",
           bsl_out[0], bsl_out[1], bsl_out[2], bsl_out[3]);
}

int main(void) {
    test_vceqq_u32();
    test_vcgtq_s16();
    test_vandq_u32();
    test_vorrq_u32();
    test_veorq_u32();
    test_vbicq_u8();
    test_vbslq_u32();
    return 0;
}
