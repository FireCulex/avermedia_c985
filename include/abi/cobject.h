/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_COBJECT_H
#define C985_COBJECT_H

#include <linux/types.h>
#include <linux/spinlock.h>

struct CObject {
    int (*Init)(struct CObject *this);      /* 0x00 */
    int (*Done)(struct CObject *this);      /* 0x08 */
    struct CObject *m_pParent;              /* 0x10 */
    int m_fInitialized;                     /* 0x18 */
    u32 m_dwObjectAttributes;               /* 0x1C */
    void *m_semCriticalSection;             /* 0x20 */
    spinlock_t m_spinlock;                  /* 0x28 */
    u8 m_irql;                              /* 0x30 */
    u8 _pad[7];                             /* 0x31 */
};                                          /* total: 0x38 */

#endif /* C985_COBJECT_H */
