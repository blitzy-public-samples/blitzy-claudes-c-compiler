// Test: NEON comparison and bitwise intrinsics
//
// Compile: ccc-arm -o test main.c
// Run:     qemu-aarch64 -L /usr/aarch64-linux-gnu ./test
//
// Exercises 128-bit NEON comparison and bitwise operations:
//   vceqq_u32  — element-wise equality comparison
//   vcgtq_s16  — element-wise signed greater-than comparison
//   vandq_u32  — bitwise AND
//   vorrq_u32  — bitwise OR
//   veorq_u32  — bitwise XOR
//   vbicq_u8   — bitwise clear (AND-NOT: a & ~b)
//   vbslq_u32  — bitwise select ((sel & a) | (~sel & b))

#include <arm_neon.h>

int printf(const char *fmt, ...);

int main(void) {
    // --- vceqq_u32: element-wise equality compare uint32x4_t ---
    // {10, 20, 30, 40} == {10, 99, 30, 77}
    // Result: {FFFFFFFF, 00000000, FFFFFFFF, 00000000}
    {
        unsigned int a_arr[4] = {10, 20, 30, 40};
        unsigned int b_arr[4] = {10, 99, 30, 77};
        uint32x4_t a = vld1q_u32(a_arr);
        uint32x4_t b = vld1q_u32(b_arr);
        uint32x4_t r = vceqq_u32(a, b);
        unsigned int out[4];
        vst1q_u32(out, r);
        printf("vceqq_u32: %08x %08x %08x %08x\n",
               out[0], out[1], out[2], out[3]);
    }

    // --- vcgtq_s16: element-wise signed greater-than int16x8_t ---
    // {5, 10, 0, 100, -5, 1, -1, 0} > {0, 0, 0, 0, 0, 0, 0, 0}
    // Result: {FFFF, FFFF, 0000, FFFF, 0000, FFFF, 0000, 0000}
    {
        int16x8_t va = {{5, 10, 0, 100, -5, 1, -1, 0}};
        int16x8_t vb = {{0, 0, 0, 0, 0, 0, 0, 0}};
        uint16x8_t r = vcgtq_s16(va, vb);
        printf("vcgtq_s16: %04x %04x %04x %04x %04x %04x %04x %04x\n",
               (unsigned)r.__val[0], (unsigned)r.__val[1],
               (unsigned)r.__val[2], (unsigned)r.__val[3],
               (unsigned)r.__val[4], (unsigned)r.__val[5],
               (unsigned)r.__val[6], (unsigned)r.__val[7]);
    }

    // --- vandq_u32: bitwise AND uint32x4_t ---
    // {FF00FF00, 12345678, ABCDEF01, 12345678}
    // & {0F000F00, 00000000, 00000000, FFFF0000}
    // = {0F000F00, 00000000, 00000000, 12340000}
    {
        unsigned int a_arr[4] = {0xFF00FF00, 0x12345678, 0xABCDEF01, 0x12345678};
        unsigned int b_arr[4] = {0x0F000F00, 0x00000000, 0x00000000, 0xFFFF0000};
        uint32x4_t a = vld1q_u32(a_arr);
        uint32x4_t b = vld1q_u32(b_arr);
        uint32x4_t r = vandq_u32(a, b);
        unsigned int out[4];
        vst1q_u32(out, r);
        printf("vandq_u32: %08x %08x %08x %08x\n",
               out[0], out[1], out[2], out[3]);
    }

    // --- vorrq_u32: bitwise OR uint32x4_t ---
    // {F00F0F05, 12345678, ABCDEF01, EDCB5678}
    // | {0F00F00A, EDCBA987, 54321EFE, 12340000}
    // = {FF0FFF0F, FFFFFFFF, FFFFFFFF, FFFF5678}
    {
        unsigned int a_arr[4] = {0xF00F0F05, 0x12345678, 0xABCDEF01, 0xEDCB5678};
        unsigned int b_arr[4] = {0x0F00F00A, 0xEDCBA987, 0x54321EFE, 0x12340000};
        uint32x4_t a = vld1q_u32(a_arr);
        uint32x4_t b = vld1q_u32(b_arr);
        uint32x4_t r = vorrq_u32(a, b);
        unsigned int out[4];
        vst1q_u32(out, r);
        printf("vorrq_u32: %08x %08x %08x %08x\n",
               out[0], out[1], out[2], out[3]);
    }

    // --- veorq_u32: bitwise XOR uint32x4_t ---
    // {FF00FF00, 12345678, ABCDEF01, EDCB5678}
    // ^ {0F0F0F0F, EDCBA987, 543210FE, 00000000}
    // = {F00FF00F, FFFFFFFF, FFFFFFFF, EDCB5678}
    {
        unsigned int a_arr[4] = {0xFF00FF00, 0x12345678, 0xABCDEF01, 0xEDCB5678};
        unsigned int b_arr[4] = {0x0F0F0F0F, 0xEDCBA987, 0x543210FE, 0x00000000};
        uint32x4_t a = vld1q_u32(a_arr);
        uint32x4_t b = vld1q_u32(b_arr);
        uint32x4_t r = veorq_u32(a, b);
        unsigned int out[4];
        vst1q_u32(out, r);
        printf("veorq_u32: %08x %08x %08x %08x\n",
               out[0], out[1], out[2], out[3]);
    }

    // --- vbicq_u8: bitwise clear (a & ~b) uint8x16_t ---
    // a = {FF,0F,FF,00, AA,0A,AA,00, 55,05,55,00, 00,00,00,00}
    // b = {0F,00,00,FF, 0A,00,00,FF, 05,00,00,FF, FF,FF,FF,FF}
    // r = {F0,0F,FF,00, A0,0A,AA,00, 50,05,55,00, 00,00,00,00}
    {
        unsigned char a_arr[16] = {0xFF, 0x0F, 0xFF, 0x00,
                                   0xAA, 0x0A, 0xAA, 0x00,
                                   0x55, 0x05, 0x55, 0x00,
                                   0x00, 0x00, 0x00, 0x00};
        unsigned char b_arr[16] = {0x0F, 0x00, 0x00, 0xFF,
                                   0x0A, 0x00, 0x00, 0xFF,
                                   0x05, 0x00, 0x00, 0xFF,
                                   0xFF, 0xFF, 0xFF, 0xFF};
        uint8x16_t a = vld1q_u8(a_arr);
        uint8x16_t b = vld1q_u8(b_arr);
        uint8x16_t r = vbicq_u8(a, b);
        unsigned char out[16];
        vst1q_u8(out, r);
        printf("vbicq_u8: %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               out[0], out[1], out[2], out[3],
               out[4], out[5], out[6], out[7],
               out[8], out[9], out[10], out[11],
               out[12], out[13], out[14], out[15]);
    }

    // --- vbslq_u32: bitwise select ((sel & a) | (~sel & b)) ---
    // sel = {11111111, AAAAAAAA, 33CC33CC, F0F0F0F0}
    // a   = {11111111, FFFFFFFF, 33CC33CC, DEDEDEDE}
    // b   = {00000000, 11111111, 00000000, 04040404}
    // r   = {11111111, BBBBBBBB, 33CC33CC, D4D4D4D4}
    {
        unsigned int sel_arr[4] = {0x11111111, 0xAAAAAAAA, 0x33CC33CC, 0xF0F0F0F0};
        unsigned int a_arr[4]   = {0x11111111, 0xFFFFFFFF, 0x33CC33CC, 0xDEDEDEDE};
        unsigned int b_arr[4]   = {0x00000000, 0x11111111, 0x00000000, 0x04040404};
        uint32x4_t sel = vld1q_u32(sel_arr);
        uint32x4_t a   = vld1q_u32(a_arr);
        uint32x4_t b   = vld1q_u32(b_arr);
        uint32x4_t r   = vbslq_u32(sel, a, b);
        unsigned int out[4];
        vst1q_u32(out, r);
        printf("vbslq_u32: %08x %08x %08x %08x\n",
               out[0], out[1], out[2], out[3]);
    }

    return 0;
}
