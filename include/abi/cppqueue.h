/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/abi/cppqueue.h — CppQueue / CDataQueue ABI layout (CppObject-based)
 *
 * Used by CDataPin for m_pFreeQueue, m_pDataRequestQueue.
 * Constructed via CppQueue::CppQueue().
 *
 * Verified from Ghidra CppQueue<PIN_DATA_REQ_*> struct:
 *   +0x00  CppObject base (0x70 bytes, shown as 14 x longlong padding)
 *   +0x70  QUEUE_LIST_CPP (pHead, pTail)
 *   +0x80  m_dwNbInQueue
 *   +0x88  m_pFuncCallback (error handler)
 *   Total: 0x90
 *
 * Verified from decompiled function bodies:
 *   CppQueue::AddEntry      accesses +0x70, +0x78, +0x80
 *   CppQueue::GetOneEntry   accesses +0x70, +0x78, +0x80
 *   CppQueue::RemoveEntry   accesses +0x70, +0x78, +0x80
 *   CppQueue::CppQueue      sets +0x80 = 0, +0x88 = callback
 *   All lock access via CppObject: +0x18, +0x28, +0x30, +0x38
 */
#ifndef _CPP_QUEUE_H
#define _CPP_QUEUE_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include "cppobject.h"
#include "../../qperrors.h"
#include "pin_data_req.h"



struct QUEUE_ENTRY_CPP {
    struct QUEUE_ENTRY_CPP *pNext;  /* 0x00 */
    struct PIN_DATA_REQ *Data;      /* 0x08 */
};                                  /* total: 0x10 */

struct QUEUE_LIST_CPP {
    struct QUEUE_ENTRY_CPP *pHead;  /* 0x00 */
    struct QUEUE_ENTRY_CPP *pTail;  /* 0x08 */
};                                  /* total: 0x10 */

struct cpp_queue {
    struct CppObject m_Object;                  /* 0x00 */
    struct QUEUE_LIST_CPP m_Queue;              /* 0x70 */
    u32 m_dwNbInQueue;                          /* 0x80 */
    u32 _pad84;                                 /* 0x84 */
    _EQPErrors (*m_pFuncCallback)(void *);      /* 0x88 */
};                                              /* total: 0x90 */

static_assert(offsetof(struct cpp_queue, m_Object)         == 0x00);
static_assert(offsetof(struct cpp_queue, m_Queue)          == 0x70);
static_assert(offsetof(struct cpp_queue, m_Queue.pHead)    == 0x70);
static_assert(offsetof(struct cpp_queue, m_Queue.pTail)    == 0x78);
static_assert(offsetof(struct cpp_queue, m_dwNbInQueue)    == 0x80);
static_assert(offsetof(struct cpp_queue, m_pFuncCallback)  == 0x88);
static_assert(sizeof(struct cpp_queue)                     == 0x90);

/*
 * CDataQueue — inherits CppQueue, no additional fields
 * Verified from CDataQueue::CDataQueue decompilation:
 *   calls CppQueue::CppQueue then sets vftable
 */
struct c_data_queue {
    struct cpp_queue base;      /* 0x00 */
};                              /* total: 0x90 */

static_assert(sizeof(struct c_data_queue) == 0x90);

#endif /* _CPP_QUEUE_H */
