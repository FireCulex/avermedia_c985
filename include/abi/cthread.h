/* include/abi/cthread.h */
#ifndef _CTHREAD_H
#define _CTHREAD_H

#include <linux/types.h>

struct CThread {
    int (*m_ThreadProc)(struct CThread *);       /* 0x00 */
    int (*ThreadExit)(struct CThread *);         /* 0x08 */
    void *m_threadID;                            /* 0x10 */
    void *m_context;                             /* 0x18 */
    u32 m_priority;                              /* 0x20 */
    u32 _pad24;                                  /* 0x24 */
    void *m_EvtWait;                             /* 0x28 */
    void *m_EvtReply;                            /* 0x30 */
    int m_bRemoveUserSpaceMapping;               /* 0x38 */
    char m_szThreadName[32];                     /* 0x3C */
    u32 _pad5C;                                  /* 0x5C */
};                                               /* total: 0x60 */

#endif /* _CTHREAD_H */
