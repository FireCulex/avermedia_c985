/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_COBJECTMGR_H
#define C985_COBJECTMGR_H

#include <linux/types.h>
#include "cobject.h"

struct CObjectEntry {
    void *pObject;                          /* 0x00 */
    u32 hObject;                            /* 0x08 */
    u32 _pad;                               /* 0x0C */
    struct CObjectEntry *pNext;             /* 0x10 */
};

struct CObjectMgr {
    struct CObject m_Object;                /* 0x00 */
    u32 m_hCurObject;                       /* 0x38 */
    u32 _pad1;                              /* 0x3C */
    struct CObjectEntry *m_pHead;           /* 0x40 */
    u32 m_dwObjectNb;                       /* 0x48 */
    u32 _pad2;                              /* 0x4C */
    void *m_pFuncCallback;                  /* 0x50 */
};

#endif /* C985_COBJECTMGR_H */
