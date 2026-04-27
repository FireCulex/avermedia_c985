/* include/abi/ihciapi.h */
#ifndef _IHCIAPI_H
#define _IHCIAPI_H

#include <linux/types.h>
#include "cobject.h"
#include "cthread.h"
#include "../../qperrors.h"

struct CQLCodec;
struct CUsbCntl;
struct CPCIeCntl;

typedef enum _QPHCI_MODE {
    QPHCI_MODE_UNKNOWN = 0,
} _QPHCI_MODE;

typedef enum _QPHCI_BUS {
    QPHCI_BUS_UNKNOWN = 0,
} _QPHCI_BUS;

struct IHCIAPI {
    struct CObject m_Object;                                        /* 0x00 */
    struct CThread m_Thread;                                        /* 0x38 */
    _EQPErrors (*ReadHciRegister)(struct IHCIAPI *, u8, u8 *);      /* 0x98 */
    _EQPErrors (*WriteHciRegister)(struct IHCIAPI *, u8, u8);       /* 0xA0 */
    _EQPErrors (*RegisterRead)(struct IHCIAPI *, u16, u32 *);       /* 0xA8 */
    _EQPErrors (*RegisterWrite)(struct IHCIAPI *, u16, u32);        /* 0xB0 */
    _EQPErrors (*RegisterReadEx)(struct IHCIAPI *, u16, u32 *, u32);  /* 0xB8 */
    _EQPErrors (*RegisterWriteEx)(struct IHCIAPI *, u16, u32 *, u32); /* 0xC0 */
    _EQPErrors (*MemoryRead)(struct IHCIAPI *, u32, u32 *);         /* 0xC8 */
    _EQPErrors (*MemoryReadEx)(struct IHCIAPI *, u32, u32, u32 *);  /* 0xD0 */
    _EQPErrors (*MemoryWrite)(struct IHCIAPI *, u32, u32);          /* 0xD8 */
    _EQPErrors (*StartDMAWrite)(struct IHCIAPI *, u32, u32, u8 *, u32, u32, u32, u32, int); /* 0xE0 */
    _EQPErrors (*StartDMARead)(struct IHCIAPI *, u32, u32, u8 *, u32, u32, u32, u32, int);  /* 0xE8 */
    _EQPErrors (*ResetArm)(struct IHCIAPI *, int);                  /* 0xF0 */
    _EQPErrors (*SetInterrupt)(struct IHCIAPI *, u32);              /* 0xF8 */
    _EQPErrors (*ClearInterrupt)(struct IHCIAPI *, u32);            /* 0x100 */
    _EQPErrors (*EnableInterrupts)(struct IHCIAPI *, u32);          /* 0x108 */
    _EQPErrors (*DisableInterrupts)(struct IHCIAPI *, u32);         /* 0x110 */
    _EQPErrors (*GetInterruptsStatus)(struct IHCIAPI *, u32 *);     /* 0x118 */
    _EQPErrors (*GetInterruptMask)(struct IHCIAPI *, u32 *);        /* 0x120 */
    _EQPErrors (*SetInterruptMask)(struct IHCIAPI *, u32);          /* 0x128 */
    _EQPErrors (*CopyFromCommonBuffer)(struct IHCIAPI *, u8 *, u32); /* 0x130 */
    _EQPErrors (*DMAReadDone)(struct IHCIAPI *);                    /* 0x138 */
    _EQPErrors (*DMAWriteDone)(struct IHCIAPI *);                   /* 0x140 */
    _EQPErrors (*DMAXferDone)(struct IHCIAPI *);                    /* 0x148 */
    struct CQLCodec *m_pMpegCodec;                                  /* 0x150 */
    _QPHCI_MODE m_access_mode;                                      /* 0x158 */
    _QPHCI_BUS m_bus_type;                                          /* 0x15C */
    u8 *m_pRegisterBase;                                            /* 0x160 */
    u8 *m_pMemoryBase;                                              /* 0x168 */
    u32 m_PageSize;                                                 /* 0x170 */
    u32 m_mem_mapping_start_addr[3];                                /* 0x174 */
    u32 m_mem_mapping_end_addr[3];                                  /* 0x180 */
    u32 m_mem_mapping_offset[3];                                    /* 0x18C */
    u32 _pad198;                                                    /* 0x198 */
    void *m_EvtEmuTask;                                             /* 0x1A0 */
    void *m_EvtEmuTaskReply;                                        /* 0x1A8 */
    struct CUsbCntl *m_pUsbCntl;                                    /* 0x1B0 */
    struct CPCIeCntl *m_pPCIeCntl;                                  /* 0x1B8 */
    int m_bEmulationMode;                                           /* 0x1C0 */
    u32 m_ulChipVer;                                                /* 0x1C4 */
};                                                                  /* total: 0x1C8 (fits in 0x1C0 rounded) */

#endif /* _IHCIAPI_H */
