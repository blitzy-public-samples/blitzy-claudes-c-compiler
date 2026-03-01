#include <arm_neon.h>

/* Forward-declare printf to avoid pulling in system stdio.h which contains
   attribute syntax not yet fully supported by CCC's parser. */
int printf(const char *fmt, ...);

int main(void) {
    /* Test 1: vmovl_u16 — widen uint16x4_t to uint32x4_t (zero-extension) */
    uint16x4_t widen_u16_in;
    widen_u16_in.__val[0] = 100;
    widen_u16_in.__val[1] = 30000;
    widen_u16_in.__val[2] = 65535;
    widen_u16_in.__val[3] = 0;
    uint32x4_t widen_u16_out = vmovl_u16(widen_u16_in);
    printf("vmovl_u16: %u %u %u %u\n",
           widen_u16_out.__val[0], widen_u16_out.__val[1],
           widen_u16_out.__val[2], widen_u16_out.__val[3]);

    /* Test 2: vmovl_s16 — widen int16x4_t to int32x4_t (sign-extension) */
    int16x4_t widen_s16_in;
    widen_s16_in.__val[0] = 100;
    widen_s16_in.__val[1] = -100;
    widen_s16_in.__val[2] = 32767;
    widen_s16_in.__val[3] = -32768;
    int32x4_t widen_s16_out = vmovl_s16(widen_s16_in);
    printf("vmovl_s16: %d %d %d %d\n",
           widen_s16_out.__val[0], widen_s16_out.__val[1],
           widen_s16_out.__val[2], widen_s16_out.__val[3]);

    /* Test 3: vmovl_high_u8 — widen high half of uint8x16_t to uint16x8_t */
    unsigned char high_u8_data[] = {0,1,2,3,4,5,6,7, 10,20,100,200,255,0,128,1};
    uint8x16_t high_u8_in = vld1q_u8(high_u8_data);
    uint16x8_t high_u8_out = vmovl_high_u8(high_u8_in);
    printf("vmovl_high_u8: %u %u %u %u %u %u %u %u\n",
           high_u8_out.__val[0], high_u8_out.__val[1],
           high_u8_out.__val[2], high_u8_out.__val[3],
           high_u8_out.__val[4], high_u8_out.__val[5],
           high_u8_out.__val[6], high_u8_out.__val[7]);

    /* Test 4: vmovn_u16 — narrow uint16x8_t to uint8x8_t (truncation, low 8 bits) */
    unsigned short narrow_u16_data[] = {100, 200, 255, 256, 0, 1, 300, 65535};
    uint16x8_t narrow_u16_in = vld1q_u16(narrow_u16_data);
    uint8x8_t narrow_u16_out = vmovn_u16(narrow_u16_in);
    printf("vmovn_u16: %u %u %u %u %u %u %u %u\n",
           narrow_u16_out.__val[0], narrow_u16_out.__val[1],
           narrow_u16_out.__val[2], narrow_u16_out.__val[3],
           narrow_u16_out.__val[4], narrow_u16_out.__val[5],
           narrow_u16_out.__val[6], narrow_u16_out.__val[7]);

    /* Test 5: vmovn_u32 — narrow uint32x4_t to uint16x4_t (truncation, low 16 bits) */
    unsigned int narrow_u32_data[] = {12345, 65536, 100000, 0};
    uint32x4_t narrow_u32_in = vld1q_u32(narrow_u32_data);
    uint16x4_t narrow_u32_out = vmovn_u32(narrow_u32_in);
    printf("vmovn_u32: %u %u %u %u\n",
           narrow_u32_out.__val[0], narrow_u32_out.__val[1],
           narrow_u32_out.__val[2], narrow_u32_out.__val[3]);

    /* Test 6: vmovn_u64 — narrow uint64x2_t to uint32x2_t (truncation, low 32 bits) */
    unsigned long long narrow_u64_data[] = {4294967296ULL, 12345ULL};
    uint64x2_t narrow_u64_in = vld1q_u64(narrow_u64_data);
    uint32x2_t narrow_u64_out = vmovn_u64(narrow_u64_in);
    printf("vmovn_u64: %u %u\n",
           narrow_u64_out.__val[0], narrow_u64_out.__val[1]);

    /* Test 7: vqmovn_s32 — saturating narrow int32x4_t to int16x4_t */
    int32x4_t sat_narrow_in;
    sat_narrow_in.__val[0] = 100000;
    sat_narrow_in.__val[1] = -100000;
    sat_narrow_in.__val[2] = 1000;
    sat_narrow_in.__val[3] = -500;
    int16x4_t sat_narrow_out = vqmovn_s32(sat_narrow_in);
    printf("vqmovn_s32: %d %d %d %d\n",
           sat_narrow_out.__val[0], sat_narrow_out.__val[1],
           sat_narrow_out.__val[2], sat_narrow_out.__val[3]);

    /* Test 8: vaddl_u8 — widening add uint8x8_t + uint8x8_t -> uint16x8_t */
    unsigned char addl_a[] = {200, 100, 255, 0, 128, 50, 1, 254};
    unsigned char addl_b[] = {100, 200, 255, 0, 128, 50, 1, 254};
    uint8x8_t addl_va = vld1_u8(addl_a);
    uint8x8_t addl_vb = vld1_u8(addl_b);
    uint16x8_t addl_out = vaddl_u8(addl_va, addl_vb);
    printf("vaddl_u8: %u %u %u %u %u %u %u %u\n",
           addl_out.__val[0], addl_out.__val[1],
           addl_out.__val[2], addl_out.__val[3],
           addl_out.__val[4], addl_out.__val[5],
           addl_out.__val[6], addl_out.__val[7]);

    /* Test 9: vsubl_u8 — widening subtract uint8x8_t - uint8x8_t -> uint16x8_t */
    unsigned char subl_a[] = {200, 100, 255, 50, 128, 0, 254, 1};
    unsigned char subl_b[] = {100, 50, 128, 0, 127, 0, 253, 1};
    uint8x8_t subl_va = vld1_u8(subl_a);
    uint8x8_t subl_vb = vld1_u8(subl_b);
    uint16x8_t subl_out = vsubl_u8(subl_va, subl_vb);
    printf("vsubl_u8: %u %u %u %u %u %u %u %u\n",
           subl_out.__val[0], subl_out.__val[1],
           subl_out.__val[2], subl_out.__val[3],
           subl_out.__val[4], subl_out.__val[5],
           subl_out.__val[6], subl_out.__val[7]);

    return 0;
}
