/*
 * ABI integration test: sizeof basic C types
 *
 * Verifies that CCC's sizeof computation for all basic C types produces
 * correct values matching GCC behavior on LP64 targets (x86-64, AArch64,
 * RISC-V 64).
 *
 * Expected LP64 sizes (from src/common/types.rs size_ctx method):
 *   sizeof(char)      = 1   (line 1305: CType::Char | CType::UChar => 1)
 *   sizeof(short)     = 2   (line 1306: CType::Short | CType::UShort => 2)
 *   sizeof(int)       = 4   (line 1307: CType::Int | CType::UInt => 4)
 *   sizeof(long)      = 8   (line 1309: CType::Long | CType::ULong => ptr_sz)
 *   sizeof(long long) = 8   (line 1310: CType::LongLong | CType::ULongLong => 8)
 *   sizeof(float)     = 4   (line 1312: CType::Float => 4)
 *   sizeof(double)    = 8   (line 1313: CType::Double => 8)
 *   sizeof(void *)    = 8   (line 1321: CType::Pointer(_, _) => ptr_sz)
 *
 * This test is skipped on i686 (ILP32) via expected.skip.i686 because
 * sizeof(long) = 4 and sizeof(void *) = 4 on that architecture.
 *
 * Unsigned variants are not tested separately because sizeof(unsigned X)
 * is always equal to sizeof(X) by C standard definition.
 *
 * long double is not tested here due to complex arch-dependent sizes
 * (12 on i686, 16 on LP64) — that is covered by a separate test.
 */

#include <stdio.h>

int main(void) {
    printf("sizeof(char) = %zu\n", sizeof(char));
    printf("sizeof(short) = %zu\n", sizeof(short));
    printf("sizeof(int) = %zu\n", sizeof(int));
    printf("sizeof(long) = %zu\n", sizeof(long));
    printf("sizeof(long long) = %zu\n", sizeof(long long));
    printf("sizeof(float) = %zu\n", sizeof(float));
    printf("sizeof(double) = %zu\n", sizeof(double));
    printf("sizeof(void *) = %zu\n", sizeof(void *));
    return 0;
}
