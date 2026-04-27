/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/abi/cqueue.h — CQueue ABI layout (CObject-based)
 *
 * Used by CChannel for free/data/pending queues.
 * Constructed via CQueue_Constructor().
 *
 * Verified from Ghidra CQueue struct + CQueue_Constructor decompilation:
 *   +0x00  CObject base (0x38 bytes)
 *   +0x38  QUEUE_LIST (pHead, pTail)
 *   +0x48  m_dwNbInQueue
 *   Total: 0x50
 */
#ifndef _C_QUEUE_H
#define _C_QUEUE_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
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
};                                              /* total: 0x50 */

static_assert(offsetof(struct c_queue, m_Object)       == 0x00);
static_assert(offsetof(struct c_queue, m_Queue)        == 0x38);
static_assert(offsetof(struct c_queue, m_Queue.pHead)  == 0x38);
static_assert(offsetof(struct c_queue, m_Queue.pTail)  == 0x40);
static_assert(offsetof(struct c_queue, m_dwNbInQueue)  == 0x48);
static_assert(sizeof(struct c_queue)                   == 0x50);

#endif /* _C_QUEUE_H */
