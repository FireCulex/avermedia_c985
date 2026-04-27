/* include/abi/_qp_large_integer.h */
#ifndef _QP_LARGE_INTEGER_H
#define _QP_LARGE_INTEGER_H

#include <linux/types.h>

typedef union _QP_LARGE_INTEGER {
    struct {
        u32 LowPart;        /* 0x00 */
        s32 HighPart;       /* 0x04 */
    } u;
    s64 QuadPart;           /* 0x00 */
} _QP_LARGE_INTEGER;        /* total: 0x08 */

#endif /* _QP_LARGE_INTEGER_H */
