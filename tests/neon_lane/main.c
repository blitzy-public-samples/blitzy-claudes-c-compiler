#include <arm_neon.h>

/* Forward-declare printf to avoid system stdio.h parse issues with CCC
   on AArch64 targets where attribute syntax is not fully supported. */
int printf(const char *fmt, ...);

/* Test 6 helper: vsetq_lane_u32 + vgetq_lane_u32 on 128-bit vector.
   Separated into its own function to keep per-function register pressure
   manageable for the AArch64 backend with inlined struct-returning intrinsics. */
static void test_setq_lane_u32(void) {
    uint32x4_t qv = vdupq_n_u32(0);
    qv = vsetq_lane_u32(10, qv, 0);
    qv = vsetq_lane_u32(20, qv, 1);
    qv = vsetq_lane_u32(30, qv, 2);
    qv = vsetq_lane_u32(40, qv, 3);
    unsigned int qv0 = vgetq_lane_u32(qv, 0);
    unsigned int qv1 = vgetq_lane_u32(qv, 1);
    unsigned int qv2 = vgetq_lane_u32(qv, 2);
    unsigned int qv3 = vgetq_lane_u32(qv, 3);
    printf("vsetq_lane_u32: %u %u %u %u\n", qv0, qv1, qv2, qv3);
}

int main(void) {
    /* Test 1: vdup_n_u8 — duplicate scalar u8 into all 8 lanes of uint8x8_t */
    uint8x8_t dup_u8 = vdup_n_u8(42);
    printf("vdup_n_u8: %u %u %u %u %u %u %u %u\n",
           vget_lane_u8(dup_u8, 0), vget_lane_u8(dup_u8, 1),
           vget_lane_u8(dup_u8, 2), vget_lane_u8(dup_u8, 3),
           vget_lane_u8(dup_u8, 4), vget_lane_u8(dup_u8, 5),
           vget_lane_u8(dup_u8, 6), vget_lane_u8(dup_u8, 7));

    /* Test 2: vdup_n_u32 — duplicate scalar u32 into both lanes of uint32x2_t */
    uint32x2_t dup_u32 = vdup_n_u32(12345);
    printf("vdup_n_u32: %u %u\n",
           vget_lane_u32(dup_u32, 0), vget_lane_u32(dup_u32, 1));

    /* Test 3: vdupq_n_s32 — duplicate scalar s32 into all 4 lanes of int32x4_t */
    int32x4_t dupq_s32 = vdupq_n_s32(-7);
    printf("vdupq_n_s32: %d %d %d %d\n",
           vgetq_lane_s32(dupq_s32, 0), vgetq_lane_s32(dupq_s32, 1),
           vgetq_lane_s32(dupq_s32, 2), vgetq_lane_s32(dupq_s32, 3));

    /* Test 4: vset_lane_u32 + vget_lane_u32 — set and read individual lanes */
    uint32x2_t sv = vdup_n_u32(0);
    sv = vset_lane_u32(100, sv, 0);
    sv = vset_lane_u32(200, sv, 1);
    printf("vset_lane_u32: %u %u\n",
           vget_lane_u32(sv, 0), vget_lane_u32(sv, 1));

    /* Test 5: vset_lane_s32 + vget_lane_s32 — signed lane set/get on int32x2_t */
    int32x2_t ss;
    ss.__val[0] = 0;
    ss.__val[1] = 0;
    ss = vset_lane_s32(-42, ss, 0);
    ss = vset_lane_s32(999, ss, 1);
    printf("vset_lane_s32: %d %d\n",
           vget_lane_s32(ss, 0), vget_lane_s32(ss, 1));

    /* Test 6: vsetq_lane_u32 + vgetq_lane_u32 — 128-bit vector lane set/get */
    test_setq_lane_u32();

    /* Test 7: vsetq_lane_u8 + vgetq_lane_u8 — sparse lane writes on 128-bit u8 */
    uint8x16_t qu8 = vdupq_n_u8(0);
    qu8 = vsetq_lane_u8(10, qu8, 0);
    qu8 = vsetq_lane_u8(20, qu8, 5);
    qu8 = vsetq_lane_u8(30, qu8, 10);
    qu8 = vsetq_lane_u8(40, qu8, 15);
    printf("vsetq_lane_u8: %u %u %u %u %u %u\n",
           vgetq_lane_u8(qu8, 0), vgetq_lane_u8(qu8, 5),
           vgetq_lane_u8(qu8, 10), vgetq_lane_u8(qu8, 15),
           vgetq_lane_u8(qu8, 1), vgetq_lane_u8(qu8, 14));

    /* Test 8: vdup_lane_u32 — duplicate a specific lane across all lanes */
    uint32x2_t dl_src = vdup_n_u32(0);
    dl_src = vset_lane_u32(42, dl_src, 0);
    dl_src = vset_lane_u32(99, dl_src, 1);
    uint32x2_t dl_res = vdup_lane_u32(dl_src, 1);
    printf("vdup_lane_u32: %u %u\n",
           vget_lane_u32(dl_res, 0), vget_lane_u32(dl_res, 1));

    return 0;
}
