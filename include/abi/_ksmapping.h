/* include/abi/_ksmapping.h */
#ifndef _KSMAPPING_H
#define _KSMAPPING_H

#include <linux/types.h>

typedef union _LARGE_INTEGER {
    struct {
        u32 LowPart;        /* 0x00 */
        s32 HighPart;       /* 0x04 */
    } u;
    s64 QuadPart;           /* 0x00 */
} _LARGE_INTEGER;           /* total: 0x08 */

struct _KSMAPPING {
    _LARGE_INTEGER PhysicalAddress;     /* 0x00 */
    u32 ByteCount;                      /* 0x08 */
    u32 Alignment;                      /* 0x0C */
};                                      /* total: 0x10 */

#endif /* _KSMAPPING_H */
