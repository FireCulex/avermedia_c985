/* include/abi/ctask.h */
#ifndef _CTASK_ABI_H
#define _CTASK_ABI_H

#include <linux/types.h>
#include "cobject.h"
#include "cthread.h"
#include "task_enums.h"
#include "task_structs.h"
#include "qp_buffer_descriptor.h"

struct CTask {
    struct CObject m_Object;                                    /* 0x00 */
    struct CThread m_Thread;                                    /* 0x38 */

    /* vtable function pointers */
    _EQPErrors (*Alloc)(struct CTask *, _TASK_TYPE, u32 *);                     /* 0x98 */
    _EQPErrors (*Release)(struct CTask *, u32);                                 /* 0xA0 */
    _EQPErrors (*Open)(struct CTask *, u32, _TASK_DATA_TYPE,
                       _CHANNEL_DIRECTION, _FIRMWARE_DATA_TYPE,
                       struct CChannel *);                                      /* 0xA8 */
    _EQPErrors (*Close)(struct CTask *, u32, _TASK_DATA_TYPE, int);             /* 0xB0 */
    _EQPErrors (*Start)(struct CTask *, u32, _TASK_DATA_TYPE);                  /* 0xB8 */
    _EQPErrors (*Stop)(struct CTask *, u32, _TASK_DATA_TYPE);                   /* 0xC0 */
    _EQPErrors (*Acquire)(struct CTask *, u32, _TASK_DATA_TYPE);                /* 0xC8 */
    _EQPErrors (*Pause)(struct CTask *, u32, _TASK_DATA_TYPE);                  /* 0xD0 */
    _EQPErrors (*Resume)(struct CTask *, u32, _TASK_DATA_TYPE);                 /* 0xD8 */
    int (*CancelBuffer)(struct CTask *, u32, _TASK_DATA_TYPE,
                        struct _QP_BUFFER_DESCRIPTOR *);                       /* 0xE0 */
    int (*NewBuffer)(struct CTask *, u32);                                      /* 0xE8 */
    int (*Flush)(struct CTask *, u32, _TASK_DATA_TYPE);                         /* 0xF0 */
    int (*FlushArm)(struct CTask *);                                            /* 0xF8 */
    _EQPErrors (*DMARequest)(struct CTask *, u32, u8 *, u32,
                             _CHANNEL_DIRECTION, int);                         /* 0x100 */
    void (*CompleteData)(struct CTask *, _CHANNEL_DIRECTION);                   /* 0x108 */
    int (*CompleteUser)(struct CTask *, struct _TASK_DATA *, _TASK_DATA_TYPE);   /* 0x110 */
    void (*CompleteArm)(struct CTask *, struct _TASK_DATA *, _TASK_DATA_TYPE);   /* 0x118 */
    void (*ProcessArmMessage)(struct CTask *);                                  /* 0x120 */
    void (*ProcessIoComplete)(struct CTask *, _CHANNEL_DIRECTION);              /* 0x128 */
    void (*ProcessDataStreaming)(struct CTask *);                                /* 0x130 */
    void (*ProcessCancelBuffer)(struct CTask *);                                /* 0x138 */
    void (*ProcessFlush)(struct CTask *, struct _TASK_DATA *, _TASK_DATA_TYPE);  /* 0x140 */
    void (*ProcessFlushArm)(struct CTask *);                                    /* 0x148 */

    /* Data members */
    struct CQLCodec *m_pMpegCodec;                              /* 0x150 */
    struct _TASK_DATA m_TaskData[8];                             /* 0x158 */
    struct _TASK_DMA_REQUEST m_dmaRequest;                       /* 0x33B8 */
    u32 _pad33D4;                                                /* 0x33D4 */
    struct _TASK_DMA_REQUEST *m_pDmaRequest;                     /* 0x33D8 */
    void *m_EvtDamReqComplete;                                   /* 0x33E0 */
    struct _TASK_IO_PENDING m_ioPending[2];                      /* 0x33E8 */
    struct _TASK_IO_PENDING *m_pPending[2];                      /* 0x3440 */
    struct CObject m_CritSectionIOPending;                       /* 0x3450 */
    u32 m_dwMaxDMASize;                                          /* 0x3488 */
    int m_StartID;                                               /* 0x348C */
    u32 m_taskIdPCMOut;                                          /* 0x3490 */
    u32 m_taskIdAESOut;                                          /* 0x3494 */
};                                                               /* total: 0x3498 */

#endif /* _CTASK_ABI_H */
