/* include/abi/cpp_object.h */
#ifndef _CPP_OBJECT_H
#define _CPP_OBJECT_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#define CPP_OBJECT_ATTR_SPINLOCK  0x01
#define CPP_OBJECT_ATTR_MUTEX     0x02

struct CppObject {
    s64 _padding_;                      /* 0x00 */
    struct CppObject *m_pParent;        /* 0x08 */
    int m_fInitialized;                 /* 0x10 */
    u32 m_dwWhoAmI;                     /* 0x14 */
    u32 m_dwObjectAttributes;           /* 0x18 */
    u32 __pad1c;                        /* 0x1C - alignment */
    void *m_semCriticalSection;         /* 0x20 */
    spinlock_t m_spinlock;              /* 0x28 */
    u8 m_irql;                          /* 0x30 */
    u8 __pad31[7];                      /* 0x31 - alignment to 0x38 */
    struct mutex m_mutex;               /* 0x38 */
};                                      /* total: 0x70 */

#endif /* _CPP_OBJECT_H */
