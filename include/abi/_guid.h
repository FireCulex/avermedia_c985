/* include/abi/_guid.h */
#ifndef _GUID_H
#define _GUID_H

#include <linux/types.h>

typedef struct _GUID {
    u32 Data1;        /* 0x00 */
    u16 Data2;        /* 0x04 */
    u16 Data3;        /* 0x06 */
    u8  Data4[8];     /* 0x08 */
} _GUID;             /* total: 0x10 */

#endif /* _GUID_H */