// Test: NEON load/store intrinsics
//
// Compile: ccc-arm -o test main.c
// Run:     qemu-aarch64 -L /usr/aarch64-linux-gnu ./test
//
// Exercises NEON load and store operations:
//   vld1q_u32    — load 4 uint32 values into uint32x4_t
//   vld1q_u8     — load 16 uint8 values into uint8x16_t
//   vld2q_u16    — load 2-element interleaved uint16 into 2 vectors
//   vld3q_u8     — load 3-element interleaved uint8 into 3 vectors
//   vld4q_u8     — load 4-element interleaved uint8 into 4 vectors
//   vst4q_u8     — store 4 vectors interleaved into memory
//   vld1q_f32    — load 4 float values into float32x4_t

#include <arm_neon.h>

int printf(const char *fmt, ...);

int main(void) {
    // --- vld1q_u32: load 4 uint32 values ---
    {
        unsigned int data[4] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xABCDEF01};
        uint32x4_t v = vld1q_u32(data);
        unsigned int out[4];
        vst1q_u32(out, v);
        printf("vld1q_u32: %x %x %x %x\n",
               out[0], out[1], out[2], out[3]);
    }

    // --- vld1q_u8: load 16 uint8 values ---
    {
        unsigned char data[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                                  0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0xFF};
        uint8x16_t v = vld1q_u8(data);
        unsigned char out[16];
        vst1q_u8(out, v);
        printf("vld1q_u8: %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               out[0], out[1], out[2], out[3],
               out[4], out[5], out[6], out[7],
               out[8], out[9], out[10], out[11],
               out[12], out[13], out[14], out[15]);
    }

    // --- vld2q_u16: load 2-element interleaved u16 ---
    // Input (interleaved pairs): {1,100, 2,200, 3,300, 4,400, 5,500, 6,600, 7,700, 8,800}
    // Deinterleaved: val[0] = {1,2,3,4,5,6,7,8}, val[1] = {100,200,...,800}
    {
        unsigned short data[16] = {1, 100, 2, 200, 3, 300, 4, 400,
                                   5, 500, 6, 600, 7, 700, 8, 800};
        uint16x8x2_t v = vld2q_u16(data);
        printf("vld2q[0]: %u %u %u %u %u %u %u %u\n",
               (unsigned)v.val[0].__val[0], (unsigned)v.val[0].__val[1],
               (unsigned)v.val[0].__val[2], (unsigned)v.val[0].__val[3],
               (unsigned)v.val[0].__val[4], (unsigned)v.val[0].__val[5],
               (unsigned)v.val[0].__val[6], (unsigned)v.val[0].__val[7]);
        printf("vld2q[1]: %u %u %u %u %u %u %u %u\n",
               (unsigned)v.val[1].__val[0], (unsigned)v.val[1].__val[1],
               (unsigned)v.val[1].__val[2], (unsigned)v.val[1].__val[3],
               (unsigned)v.val[1].__val[4], (unsigned)v.val[1].__val[5],
               (unsigned)v.val[1].__val[6], (unsigned)v.val[1].__val[7]);
    }

    // --- vld3q_u8: load 3-element interleaved u8 ---
    // Input: 0,1,2, 3,4,5, 6,7,8, ..., 45,46,47
    // Deinterleaved val[0]: indices 0,3,6,...,45 = {0,3,6,9,...,45}
    {
        unsigned char data[48];
        for (int i = 0; i < 48; i++) data[i] = (unsigned char)i;
        uint8x16x3_t v = vld3q_u8(data);
        printf("vld3q[0]: %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               (unsigned)v.val[0].__val[0],  (unsigned)v.val[0].__val[1],
               (unsigned)v.val[0].__val[2],  (unsigned)v.val[0].__val[3],
               (unsigned)v.val[0].__val[4],  (unsigned)v.val[0].__val[5],
               (unsigned)v.val[0].__val[6],  (unsigned)v.val[0].__val[7],
               (unsigned)v.val[0].__val[8],  (unsigned)v.val[0].__val[9],
               (unsigned)v.val[0].__val[10], (unsigned)v.val[0].__val[11],
               (unsigned)v.val[0].__val[12], (unsigned)v.val[0].__val[13],
               (unsigned)v.val[0].__val[14], (unsigned)v.val[0].__val[15]);
    }

    // --- vld4q_u8: load 4-element interleaved u8 ---
    // Input: 0,1,2,3, 4,5,6,7, ..., 60,61,62,63
    // Deinterleaved val[0]: indices 0,4,8,...,60 = {0,4,8,...,60}
    {
        unsigned char data[64];
        for (int i = 0; i < 64; i++) data[i] = (unsigned char)i;
        uint8x16x4_t v = vld4q_u8(data);
        printf("vld4q[0]: %02x %02x %02x %02x %02x %02x %02x %02x "
               "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               (unsigned)v.val[0].__val[0],  (unsigned)v.val[0].__val[1],
               (unsigned)v.val[0].__val[2],  (unsigned)v.val[0].__val[3],
               (unsigned)v.val[0].__val[4],  (unsigned)v.val[0].__val[5],
               (unsigned)v.val[0].__val[6],  (unsigned)v.val[0].__val[7],
               (unsigned)v.val[0].__val[8],  (unsigned)v.val[0].__val[9],
               (unsigned)v.val[0].__val[10], (unsigned)v.val[0].__val[11],
               (unsigned)v.val[0].__val[12], (unsigned)v.val[0].__val[13],
               (unsigned)v.val[0].__val[14], (unsigned)v.val[0].__val[15]);
    }

    // --- vst4q_u8: store 4 vectors interleaved ---
    // val[0] = all 0xAA, val[1] = all 0xBB, val[2] = all 0xCC, val[3] = all 0xDD
    // Interleaved output: AA BB CC DD AA BB CC DD ...
    {
        uint8x16x4_t v;
        v.val[0] = vdupq_n_u8(0xAA);
        v.val[1] = vdupq_n_u8(0xBB);
        v.val[2] = vdupq_n_u8(0xCC);
        v.val[3] = vdupq_n_u8(0xDD);
        unsigned char out[64];
        vst4q_u8(out, v);
        printf("vst4q_u8: %02x %02x %02x %02x %02x %02x %02x %02x\n",
               out[0], out[1], out[2], out[3],
               out[4], out[5], out[6], out[7]);
    }

    // --- vld1q_f32: load 4 float values ---
    {
        float data[4] = {1.5f, 2.25f, 3.125f, 4.0625f};
        float32x4_t v = vld1q_f32(data);
        printf("vld1q_f32: %.4f %.4f %.4f %.4f\n",
               (double)v.__val[0], (double)v.__val[1],
               (double)v.__val[2], (double)v.__val[3]);
    }

    return 0;
}
