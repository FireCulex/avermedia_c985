/* include/abi/ksstream_pointer_offset.h */
#ifndef _KSSTREAM_POINTER_OFFSET_H
#define _KSSTREAM_POINTER_OFFSET_H

#include <linux/types.h>
#include "_ksmapping.h"

union _KSSTREAM_POINTER_OFFSET_u {
    u8 *Data;
    struct _KSMAPPING *Mappings;
};

struct _KSSTREAM_POINTER_OFFSET {
    union _KSSTREAM_POINTER_OFFSET_u u;  /* 0x00 */
    u32 Count;                            /* 0x08 */
    u32 Remaining;                        /* 0x0C */
};                                        /* total: 0x10 */

#endif /* _KSSTREAM_POINTER_OFFSET_H */
