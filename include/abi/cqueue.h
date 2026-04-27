/* include/abi/c_queue.h */
#ifndef _C_QUEUE_H
#define _C_QUEUE_H

#include <linux/types.h>
#include "cobject.h"
#include "../../qperrors.h"

struct QUEUE_ENTRY {
    void *pNext;                /* 0x00 */
    void *Data;                 /* 0x08 */
};                              /* total: 0x10 */

struct QUEUE_LIST {
    struct QUEUE_ENTRY *pHead;  /* 0x00 */
    struct QUEUE_ENTRY *pTail;  /* 0x08 */
};                              /* total: 0x10 */

struct c_queue {
    struct CObject m_Object;                    /* 0x00 */
    struct QUEUE_LIST m_Queue;                  /* 0x38 */
    u32 m_dwNbInQueue;                          /* 0x48 */
    u32 _pad4C;                                 /* 0x4C */
    u8 _padding[0x30];                          /* 0x50 - 0x7F */
    u32 m_dwExtra;                              /* 0x80 */
    u32 _pad84;                                 /* 0x84 */
    _EQPErrors (*m_pErrorHandler)(void *);      /* 0x88 */
};                                              /* total: 0x90 */

struct c_data_queue {
    struct c_queue base;        /* 0x00 - 0x8F */
};                              /* total: 0x90 */

#endif /* _C_QUEUE_H */
