#include <arm_neon.h>

int printf(const char *fmt, ...);

/* Test 1: vld1q_u32 / vst1q_u32 — basic 128-bit load/store round-trip */
static void test_vld1q_u32(void) {
    unsigned int data1[] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xABCDEF01};
    uint32x4_t v1 = vld1q_u32(data1);
    unsigned int out1[4];
    vst1q_u32(out1, v1);
    printf("vld1q_u32: %08x %08x %08x %08x\n",
           out1[0], out1[1], out1[2], out1[3]);
}

/* Test 2: vld1q_u8 / vst1q_u8 — byte-level 128-bit load/store */
static void test_vld1q_u8(void) {
    unsigned char data2[] = {0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,
                             0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0xFF};
    uint8x16_t v2 = vld1q_u8(data2);
    unsigned char out2[16];
    vst1q_u8(out2, v2);
    printf("vld1q_u8:");
    for (int i = 0; i < 16; i++)
        printf(" %02x", out2[i]);
    printf("\n");
}

/* Test 3: vld2q_u16 — 2-element de-interleaved load */
static void test_vld2q_u16(void) {
    unsigned short il2[16] = {1,100, 2,200, 3,300, 4,400,
                              5,500, 6,600, 7,700, 8,800};
    uint16x8x2_t v3 = vld2q_u16(il2);
    unsigned short out3a[8], out3b[8];
    vst1q_u16(out3a, v3.val[0]);
    vst1q_u16(out3b, v3.val[1]);
    printf("vld2q[0]:");
    for (int i = 0; i < 8; i++)
        printf(" %u", out3a[i]);
    printf("\n");
    printf("vld2q[1]:");
    for (int i = 0; i < 8; i++)
        printf(" %u", out3b[i]);
    printf("\n");
}

/* Test 4: vld3q_u8 — 3-element de-interleaved load (RGB-like) */
static void test_vld3q_u8(void) {
    unsigned char data4[48];
    for (int i = 0; i < 48; i++)
        data4[i] = (unsigned char)i;
    uint8x16x3_t v4 = vld3q_u8(data4);
    unsigned char out4[16];
    vst1q_u8(out4, v4.val[0]);
    printf("vld3q[0]:");
    for (int i = 0; i < 16; i++)
        printf(" %02x", out4[i]);
    printf("\n");
}

/* Test 5: vld4q_u8 — 4-element de-interleaved load (RGBA-like) */
static void test_vld4q_u8(void) {
    unsigned char data5[64];
    for (int i = 0; i < 64; i++)
        data5[i] = (unsigned char)i;
    uint8x16x4_t v5 = vld4q_u8(data5);
    unsigned char out5[16];
    vst1q_u8(out5, v5.val[0]);
    printf("vld4q[0]:");
    for (int i = 0; i < 16; i++)
        printf(" %02x", out5[i]);
    printf("\n");
}

/* Test 6: vst4q_u8 — 4-element interleaved store */
static void test_vst4q_u8(void) {
    uint8x16x4_t st4;
    for (int i = 0; i < 16; i++) {
        st4.val[0].__val[i] = 0xAA;
        st4.val[1].__val[i] = 0xBB;
        st4.val[2].__val[i] = 0xCC;
        st4.val[3].__val[i] = 0xDD;
    }
    unsigned char st4_out[64];
    vst4q_u8(st4_out, st4);
    printf("vst4q_u8:");
    for (int i = 0; i < 8; i++)
        printf(" %02x", st4_out[i]);
    printf("\n");
}

/* Test 7: vld1q_f32 / vst1q_f32 — float vector load/store */
static void test_vld1q_f32(void) {
    float fdata[] = {1.5f, 2.25f, 3.125f, 4.0625f};
    float32x4_t vf = vld1q_f32(fdata);
    float fout[4];
    vst1q_f32(fout, vf);
    printf("vld1q_f32: %.4f %.4f %.4f %.4f\n",
           fout[0], fout[1], fout[2], fout[3]);
}

int main(void) {
    test_vld1q_u32();
    test_vld1q_u8();
    test_vld2q_u16();
    test_vld3q_u8();
    test_vld4q_u8();
    test_vst4q_u8();
    test_vld1q_f32();
    return 0;
}
