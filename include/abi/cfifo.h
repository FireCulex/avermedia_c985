/* include/abi/cfifo.h */
#ifndef _CFIFO_H
#define _CFIFO_H

#include <linux/types.h>
#include "cobject.h"

struct CFifo {
    struct CObject m_Object;    /* 0x00 */
    u32 m_dwReadPtr;            /* 0x38 */
    u32 m_dwWritePtr;           /* 0x3C */
    u8 *m_Fifo;                 /* 0x40 */
    u32 m_dwFifoLevel;          /* 0x48 */
    u32 m_size;                 /* 0x4C */
    u32 m_sizeEntry;            /* 0x50 */
    u32 _pad54;                 /* 0x54 */
};                              /* total: 0x58 */

#endif /* _CFIFO_H */
