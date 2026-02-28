// Test: NEON lane manipulation intrinsics
//
// Compile: ccc-arm -o test main.c
// Run:     qemu-aarch64 -L /usr/aarch64-linux-gnu ./test
//
// Exercises NEON lane manipulation operations:
//   vdup_n_u8     — broadcast scalar u8 to all lanes of uint8x8_t
//   vdup_n_u32    — broadcast scalar u32 to both lanes of uint32x2_t
//   vdupq_n_s32   — broadcast scalar s32 to all 4 lanes of int32x4_t
//   vset_lane_u32 — set one lane in uint32x2_t
//   vset_lane_s32 — set one lane in int32x2_t
//   vsetq_lane_u32— set one lane in uint32x4_t
//   vsetq_lane_u8 — set one lane in uint8x16_t
//   vdup_lane_u32 — duplicate one lane of uint32x2_t (simulated via
//                   vget_lane_u32 + vdup_n_u32)

#include <arm_neon.h>

int printf(const char *fmt, ...);

int main(void) {
    // --- vdup_n_u8: broadcast scalar 42 to all 8 lanes ---
    // Result: {42, 42, 42, 42, 42, 42, 42, 42}
    {
        uint8x8_t v = vdup_n_u8(42);
        printf("vdup_n_u8: %u %u %u %u %u %u %u %u\n",
               (unsigned)v.__val[0], (unsigned)v.__val[1],
               (unsigned)v.__val[2], (unsigned)v.__val[3],
               (unsigned)v.__val[4], (unsigned)v.__val[5],
               (unsigned)v.__val[6], (unsigned)v.__val[7]);
    }

    // --- vdup_n_u32: broadcast scalar 0x12345 to both lanes ---
    // Result: {0x12345, 0x12345}
    {
        uint32x2_t v = vdup_n_u32(0x12345);
        printf("vdup_n_u32: %x %x\n",
               v.__val[0], v.__val[1]);
    }

    // --- vdupq_n_s32: broadcast scalar -7 to all 4 lanes ---
    // Result: {-7, -7, -7, -7}
    {
        int32x4_t v = vdupq_n_s32(-7);
        printf("vdupq_n_s32: %d %d %d %d\n",
               v.__val[0], v.__val[1], v.__val[2], v.__val[3]);
    }

    // --- vset_lane_u32: set lane 1 to 200 in {100, 0} ---
    // Result: {100, 200}
    {
        uint32x2_t v = vdup_n_u32(0);
        v = vset_lane_u32(100, v, 0);
        v = vset_lane_u32(200, v, 1);
        printf("vset_lane_u32: %u %u\n", v.__val[0], v.__val[1]);
    }

    // --- vset_lane_s32: set lanes in int32x2_t ---
    // Result: {-42, 999}
    {
        int32x2_t v = {{0, 0}};
        v = vset_lane_s32(-42, v, 0);
        v = vset_lane_s32(999, v, 1);
        printf("vset_lane_s32: %d %d\n", v.__val[0], v.__val[1]);
    }

    // --- vsetq_lane_u32: set all 4 lanes individually ---
    // Result: {10, 20, 30, 40}
    {
        uint32x4_t v = vdupq_n_u32(0);
        v = vsetq_lane_u32(10, v, 0);
        v = vsetq_lane_u32(20, v, 1);
        v = vsetq_lane_u32(30, v, 2);
        v = vsetq_lane_u32(40, v, 3);
        printf("vsetq_lane_u32: %u %u %u %u\n",
               v.__val[0], v.__val[1], v.__val[2], v.__val[3]);
    }

    // --- vsetq_lane_u8: set selected lanes in uint8x16_t ---
    // Start with all zeros, set lanes 0-3 to {10,20,30,40}
    // Result (showing first 6 lanes): {10, 20, 30, 40, 0, 0}
    {
        uint8x16_t v = vdupq_n_u8(0);
        v = vsetq_lane_u8(10, v, 0);
        v = vsetq_lane_u8(20, v, 1);
        v = vsetq_lane_u8(30, v, 2);
        v = vsetq_lane_u8(40, v, 3);
        printf("vsetq_lane_u8: %u %u %u %u %u %u\n",
               (unsigned)v.__val[0], (unsigned)v.__val[1],
               (unsigned)v.__val[2], (unsigned)v.__val[3],
               (unsigned)v.__val[4], (unsigned)v.__val[5]);
    }

    // --- vdup_lane_u32: duplicate lane 0 of {99, 77} to both lanes ---
    // Simulated via: vget_lane_u32 to extract + vdup_n_u32 to broadcast
    // Result: {99, 99}
    {
        uint32x2_t src = vdup_n_u32(0);
        src = vset_lane_u32(99, src, 0);
        src = vset_lane_u32(77, src, 1);
        // Duplicate lane 0 to both lanes
        unsigned int lane_val = vget_lane_u32(src, 0);
        uint32x2_t r = vdup_n_u32(lane_val);
        printf("vdup_lane_u32: %u %u\n", r.__val[0], r.__val[1]);
    }

    return 0;
}
