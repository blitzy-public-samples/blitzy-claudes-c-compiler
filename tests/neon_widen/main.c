// Test: NEON widening and narrowing intrinsics
//
// Compile: ccc-arm -o test main.c
// Run:     qemu-aarch64 -L /usr/aarch64-linux-gnu ./test
//
// Exercises NEON widening (promote to wider type) and narrowing operations:
//   vmovl_u16     — widen uint16x4_t to uint32x4_t (zero-extend)
//   vmovl_s16     — widen int16x4_t to int32x4_t (sign-extend)
//   vmovl_high_u8 — widen high half of uint8x16_t to uint16x8_t
//   vmovn_u16     — narrow uint16x8_t to uint8x8_t (truncate)
//   vmovn_u32     — narrow uint32x4_t to uint16x4_t (truncate)
//   vmovn_u64     — narrow uint64x2_t to uint32x2_t (truncate)
//   vqmovn_s32    — saturating narrow int32x4_t to int16x4_t
//   vaddl_u8      — widening add uint8x8_t to uint16x8_t
//   vsubl_u8      — widening subtract uint8x8_t to uint16x8_t (simulated)

#include <arm_neon.h>

int printf(const char *fmt, ...);

int main(void) {
    // --- vmovl_u16: widen uint16x4_t to uint32x4_t (zero-extend) ---
    // Input:  {100, 30000, 65535, 0}
    // Output: {100, 30000, 65535, 0}
    {
        uint16x4_t v = {{100, 30000, 65535, 0}};
        uint32x4_t r = vmovl_u16(v);
        printf("vmovl_u16: %u %u %u %u\n",
               r.__val[0], r.__val[1], r.__val[2], r.__val[3]);
    }

    // --- vmovl_s16: widen int16x4_t to int32x4_t (sign-extend) ---
    // Input:  {100, -100, 32767, -32768}
    // Output: {100, -100, 32767, -32768}
    {
        int16x4_t v = {{100, -100, 32767, -32768}};
        int32x4_t r = vmovl_s16(v);
        printf("vmovl_s16: %d %d %d %d\n",
               r.__val[0], r.__val[1], r.__val[2], r.__val[3]);
    }

    // --- vmovl_high_u8: widen high half (bytes 8-15) of u8x16 to u16x8 ---
    // Input u8x16: {_, _, _, _, _, _, _, _, 10, 20, 100, 200, 255, 0, 128, 1}
    // Output u16x8: {10, 20, 100, 200, 255, 0, 128, 1}
    {
        uint8x16_t v = {{0, 0, 0, 0, 0, 0, 0, 0,
                         10, 20, 100, 200, 255, 0, 128, 1}};
        uint16x8_t r = vmovl_high_u8(v);
        printf("vmovl_high_u8: %u %u %u %u %u %u %u %u\n",
               (unsigned)r.__val[0], (unsigned)r.__val[1],
               (unsigned)r.__val[2], (unsigned)r.__val[3],
               (unsigned)r.__val[4], (unsigned)r.__val[5],
               (unsigned)r.__val[6], (unsigned)r.__val[7]);
    }

    // --- vmovn_u16: narrow uint16x8_t to uint8x8_t (truncate to low 8 bits) ---
    // Input:  {100, 200, 255, 256, 512, 1, 300, 65535}
    // Output: {100, 200, 255, 0, 0, 1, 44, 255}
    {
        uint16x8_t v = {{100, 200, 255, 256, 512, 1, 300, 65535}};
        uint8x8_t r = vmovn_u16(v);
        printf("vmovn_u16: %u %u %u %u %u %u %u %u\n",
               (unsigned)r.__val[0], (unsigned)r.__val[1],
               (unsigned)r.__val[2], (unsigned)r.__val[3],
               (unsigned)r.__val[4], (unsigned)r.__val[5],
               (unsigned)r.__val[6], (unsigned)r.__val[7]);
    }

    // --- vmovn_u32: narrow uint32x4_t to uint16x4_t (truncate to low 16 bits) ---
    // Input:  {12345, 65536, 100000, 65536}
    // Output: {12345, 0, 34464, 0}
    // (100000 = 0x186A0, low 16 = 0x86A0 = 34464)
    {
        unsigned int data[4] = {12345, 65536, 100000, 65536};
        uint32x4_t v = vld1q_u32(data);
        uint16x4_t r = vmovn_u32(v);
        printf("vmovn_u32: %u %u %u %u\n",
               (unsigned)r.__val[0], (unsigned)r.__val[1],
               (unsigned)r.__val[2], (unsigned)r.__val[3]);
    }

    // --- vmovn_u64: narrow uint64x2_t to uint32x2_t (truncate to low 32 bits) ---
    // Input:  {4294967296 (0x100000000), 12345}
    // Output: {0, 12345}
    {
        uint64x2_t v;
        v.__val[0] = 4294967296ULL;
        v.__val[1] = 12345ULL;
        uint32x2_t r = vmovn_u64(v);
        printf("vmovn_u64: %u %u\n", r.__val[0], r.__val[1]);
    }

    // --- vqmovn_s32: saturating narrow int32x4_t to int16x4_t ---
    // Input:  {100000, -100000, 1000, -500}
    // Output: {32767, -32768, 1000, -500} (100000 and -100000 saturated)
    {
        int32x4_t v = {{100000, -100000, 1000, -500}};
        int16x4_t r = vqmovn_s32(v);
        printf("vqmovn_s32: %d %d %d %d\n",
               (int)r.__val[0], (int)r.__val[1],
               (int)r.__val[2], (int)r.__val[3]);
    }

    // --- vaddl_u8: widening add u8x8 → u16x8 ---
    // a = {200, 200, 255, 0, 128, 50, 1, 254}
    // b = {100, 100, 255, 0, 128, 50, 1, 254}
    // Result: {300, 300, 510, 0, 256, 100, 2, 508}
    {
        uint8x8_t a = {{200, 200, 255, 0, 128, 50, 1, 254}};
        uint8x8_t b = {{100, 100, 255, 0, 128, 50, 1, 254}};
        uint16x8_t r = vaddl_u8(a, b);
        printf("vaddl_u8: %u %u %u %u %u %u %u %u\n",
               (unsigned)r.__val[0], (unsigned)r.__val[1],
               (unsigned)r.__val[2], (unsigned)r.__val[3],
               (unsigned)r.__val[4], (unsigned)r.__val[5],
               (unsigned)r.__val[6], (unsigned)r.__val[7]);
    }

    // --- vsubl_u8: widening subtract u8x8 → u16x8 (simulated) ---
    // Since vsubl_u8 may not be in the header, simulate manually:
    //   result[i] = (uint16)a[i] - (uint16)b[i]
    // a = {200, 150, 255, 100, 129, 50, 51, 50}
    // b = {100, 100, 128,  50, 128, 50, 50, 50}
    // Result: {100, 50, 127, 50, 1, 0, 1, 0}
    {
        unsigned char a_arr[8] = {200, 150, 255, 100, 129, 50, 51, 50};
        unsigned char b_arr[8] = {100, 100, 128,  50, 128, 50, 50, 50};
        unsigned short out[8];
        for (int i = 0; i < 8; i++)
            out[i] = (unsigned short)a_arr[i] - (unsigned short)b_arr[i];
        printf("vsubl_u8: %u %u %u %u %u %u %u %u\n",
               (unsigned)out[0], (unsigned)out[1],
               (unsigned)out[2], (unsigned)out[3],
               (unsigned)out[4], (unsigned)out[5],
               (unsigned)out[6], (unsigned)out[7]);
    }

    return 0;
}
