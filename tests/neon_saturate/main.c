#include <arm_neon.h>

/* Forward-declare printf to avoid system stdio.h parse issues with CCC */
int printf(const char *fmt, ...);

int main(void) {
    /* Test 1: vqaddq_u8 — saturating add uint8x16_t (clamp at 255) */
    {
        unsigned char qa_a[] = {200, 10, 255, 0, 128, 50, 254, 100,
                                200, 10, 255, 0, 128, 50, 254, 100};
        unsigned char qa_b[] = {100, 20, 1, 0, 128, 50, 2, 155,
                                100, 20, 1, 0, 128, 50, 2, 155};
        uint8x16_t va8 = vld1q_u8(qa_a);
        uint8x16_t vb8 = vld1q_u8(qa_b);
        uint8x16_t qadd_res = vqaddq_u8(va8, vb8);
        unsigned char qadd_out[16];
        vst1q_u8(qadd_out, qadd_res);
        printf("vqaddq_u8:");
        for (int i = 0; i < 16; i++)
            printf(" %02x", qadd_out[i]);
        printf("\n");
    }

    /* Test 2: vqsubq_u8 — saturating subtract uint8x16_t (clamp at 0) */
    {
        unsigned char qs_a[] = {10, 200, 0, 128, 50, 0, 1, 100,
                                10, 200, 0, 128, 50, 0, 1, 100};
        unsigned char qs_b[] = {20, 100, 1, 128, 100, 0, 2, 50,
                                20, 100, 1, 128, 100, 0, 2, 50};
        uint8x16_t vs8a = vld1q_u8(qs_a);
        uint8x16_t vs8b = vld1q_u8(qs_b);
        uint8x16_t qsub_res = vqsubq_u8(vs8a, vs8b);
        unsigned char qsub_out[16];
        vst1q_u8(qsub_out, qsub_res);
        printf("vqsubq_u8:");
        for (int i = 0; i < 16; i++)
            printf(" %02x", qsub_out[i]);
        printf("\n");
    }

    /* Test 3: vqaddq_s16 — saturating add int16x8_t (clamp at -32768/32767) */
    {
        int16x8_t sa16, sb16;
        sa16.__val[0] = 30000;   sb16.__val[0] = 5000;
        sa16.__val[1] = -30000;  sb16.__val[1] = -5000;
        sa16.__val[2] = 10000;   sb16.__val[2] = 20000;
        sa16.__val[3] = 32767;   sb16.__val[3] = 1;
        sa16.__val[4] = -32768;  sb16.__val[4] = -1;
        sa16.__val[5] = 0;       sb16.__val[5] = 0;
        sa16.__val[6] = 100;     sb16.__val[6] = -100;
        sa16.__val[7] = -1000;   sb16.__val[7] = 500;
        int16x8_t qadd16_res = vqaddq_s16(sa16, sb16);
        printf("vqaddq_s16: %d %d %d %d %d %d %d %d\n",
               qadd16_res.__val[0], qadd16_res.__val[1],
               qadd16_res.__val[2], qadd16_res.__val[3],
               qadd16_res.__val[4], qadd16_res.__val[5],
               qadd16_res.__val[6], qadd16_res.__val[7]);
    }

    /* Test 4: vqsubq_u16 — saturating subtract uint16x8_t (clamp at 0) */
    {
        unsigned short su_a[] = {100, 5000, 0, 32768, 10, 65535, 1, 50000};
        unsigned short su_b[] = {200, 3000, 1, 32768, 100, 0, 2, 60000};
        uint16x8_t vu16a = vld1q_u16(su_a);
        uint16x8_t vu16b = vld1q_u16(su_b);
        uint16x8_t qsub16_res = vqsubq_u16(vu16a, vu16b);
        unsigned short qsub16_out[8];
        vst1q_u16(qsub16_out, qsub16_res);
        printf("vqsubq_u16: %u %u %u %u %u %u %u %u\n",
               qsub16_out[0], qsub16_out[1], qsub16_out[2], qsub16_out[3],
               qsub16_out[4], qsub16_out[5], qsub16_out[6], qsub16_out[7]);
    }

    /* Test 5: vqaddq_s32 — saturating add int32x4_t (clamp at INT32_MIN/MAX) */
    {
        int32x4_t s32a, s32b;
        s32a.__val[0] = 2000000000;  s32b.__val[0] = 200000000;
        s32a.__val[1] = -2000000000; s32b.__val[1] = -200000000;
        s32a.__val[2] = 1000000;     s32b.__val[2] = 2000000;
        s32a.__val[3] = -1000;       s32b.__val[3] = 500;
        int32x4_t qadd32_res = vqaddq_s32(s32a, s32b);
        printf("vqaddq_s32: %d %d %d %d\n",
               qadd32_res.__val[0], qadd32_res.__val[1],
               qadd32_res.__val[2], qadd32_res.__val[3]);
    }

    /* Test 6: vqmovn_s32 — saturating narrow int32x4_t to int16x4_t */
    {
        int32x4_t narrow_in;
        narrow_in.__val[0] = 100000;
        narrow_in.__val[1] = -100000;
        narrow_in.__val[2] = 1000;
        narrow_in.__val[3] = -500;
        int16x4_t narrow_res = vqmovn_s32(narrow_in);
        printf("vqmovn_s32: %d %d %d %d\n",
               narrow_res.__val[0], narrow_res.__val[1],
               narrow_res.__val[2], narrow_res.__val[3]);
    }

    /* Test 7: vqdmulhq_s16 — saturating doubling multiply high int16x8_t
       Formula: result[i] = sat_s32(a[i] * b[i] * 2) >> 16 (arithmetic shift)
       The key saturation case: -32768 * -32768 would give 2^31 when doubled,
       which overflows int32, so it saturates to INT32_MAX = 2147483647,
       then >>16 gives 32767. */
    {
        int16x8_t ma, mb;
        ma.__val[0] = -32768; mb.__val[0] = -32768;
        ma.__val[1] = 32767;  mb.__val[1] = 32767;
        ma.__val[2] = 16384;  mb.__val[2] = 16384;
        ma.__val[3] = 1000;   mb.__val[3] = 1000;
        ma.__val[4] = -100;   mb.__val[4] = 200;
        ma.__val[5] = 0;      mb.__val[5] = 32767;
        ma.__val[6] = 1;      mb.__val[6] = 1;
        ma.__val[7] = -32768; mb.__val[7] = 1;
        int16x8_t qdmul_res = vqdmulhq_s16(ma, mb);
        printf("vqdmulhq_s16: %d %d %d %d %d %d %d %d\n",
               qdmul_res.__val[0], qdmul_res.__val[1],
               qdmul_res.__val[2], qdmul_res.__val[3],
               qdmul_res.__val[4], qdmul_res.__val[5],
               qdmul_res.__val[6], qdmul_res.__val[7]);
    }

    return 0;
}
