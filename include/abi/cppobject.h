/* include/abi/cppobject.h */
#ifndef _CPP_OBJECT_H
#define _CPP_OBJECT_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>

#define CPP_OBJECT_ATTR_SPINLOCK  0x01
#define CPP_OBJECT_ATTR_MUTEX     0x02

struct CppObject {
    s64 _padding_;                      /* 0x00 */
    struct CppObject *m_pParent;        /* 0x08 */
    int m_fInitialized;                 /* 0x10 */
    u32 m_dwWhoAmI;                     /* 0x14 */
    u32 m_dwObjectAttributes;           /* 0x18 */
    u32 __pad1c;                        /* 0x1C */
    void *m_semCriticalSection;         /* 0x20 */
    spinlock_t m_spinlock;              /* 0x28 */
    u32 __pad2c;                        /* 0x2C */
    u8 m_irql;                          /* 0x30 */
    u8 __pad31[7];                      /* 0x31 */
    struct mutex m_mutex;               /* 0x38 */
    u8 __tailpad[0x70 - 0x38 - sizeof(struct mutex)];
};

static_assert(offsetof(struct CppObject, m_pParent) == 0x08);
static_assert(offsetof(struct CppObject, m_fInitialized) == 0x10);
static_assert(offsetof(struct CppObject, m_dwWhoAmI) == 0x14);
static_assert(offsetof(struct CppObject, m_dwObjectAttributes) == 0x18);
static_assert(offsetof(struct CppObject, m_semCriticalSection) == 0x20);
static_assert(offsetof(struct CppObject, m_spinlock) == 0x28);
static_assert(offsetof(struct CppObject, m_irql) == 0x30);
static_assert(offsetof(struct CppObject, m_mutex) == 0x38);
static_assert(sizeof(struct CppObject) == 0x70);

#endif /* _CPP_OBJECT_H */
