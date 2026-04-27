/* include/abi/cqlcodec.h */
#ifndef _CQLCODEC_H
#define _CQLCODEC_H

#include <linux/types.h>
#include "cobject.h"
#include "cobjectmgr.h"
#include "ihciapi.h"
#include "task_structs.h"
#include "../../qperrors.h"

struct CTask;
struct CUsbCntl;
struct CPCIeCntl;
struct CUsbInterface;

typedef enum _QPCODEC_MEM_TYPE {
    QPCODEC_MEM_TYPE_UNKNOWN = 0,
} _QPCODEC_MEM_TYPE;

typedef enum _QPCODEC_CHIP_TYPE {
    QPCODEC_CHIP_TYPE_UNKNOWN = 0,
} _QPCODEC_CHIP_TYPE;

/* IMpegCodec interface vtable */
struct IMpegCodec {
    _EQPErrors (*InitDevice)(struct IMpegCodec *, _EQPErrors (*)(void *, u32, void *), void *);  /* 0x00 */
    _EQPErrors (*Release)(struct IMpegCodec *);                     /* 0x08 */
    _EQPErrors (*Reset)(struct IMpegCodec *);                       /* 0x10 */
    _EQPErrors (*Set)(struct IMpegCodec *, struct _TQP_GUID *, u32, u32, void *, void *, u32);   /* 0x18 */
    _EQPErrors (*Get)(struct IMpegCodec *, struct _TQP_GUID *, u32, u32, void *, void *, u32 *); /* 0x20 */
    _EQPErrors (*AllocEncodeTask)(struct IMpegCodec *, u32 *);      /* 0x28 */
    _EQPErrors (*AllocDecodeTask)(struct IMpegCodec *, u32 *);      /* 0x30 */
    _EQPErrors (*ReleaseTask)(struct IMpegCodec *, u32);            /* 0x38 */
    _EQPErrors (*Open)(struct IMpegCodec *, u32, u32, void *, u32 *,
                       _EQPErrors (*)(void *, u32, void *), void *); /* 0x40 */
    _EQPErrors (*Close)(struct IMpegCodec *, u32);                  /* 0x48 */
    _EQPErrors (*Start)(struct IMpegCodec *, u32);                  /* 0x50 */
    _EQPErrors (*Stop)(struct IMpegCodec *, u32);                   /* 0x58 */
    _EQPErrors (*Acquire)(struct IMpegCodec *, u32);                /* 0x60 */
    _EQPErrors (*Pause)(struct IMpegCodec *, u32);                  /* 0x68 */
    _EQPErrors (*Step)(struct IMpegCodec *, u32, u32);              /* 0x70 */
    _EQPErrors (*SetRate)(struct IMpegCodec *, u32, int);           /* 0x78 */
    _EQPErrors (*GetRate)(struct IMpegCodec *, u32, int *);         /* 0x80 */
    _EQPErrors (*BeginFlush)(struct IMpegCodec *, u32);             /* 0x88 */
    _EQPErrors (*Flush)(struct IMpegCodec *, u32);                  /* 0x90 */
    _EQPErrors (*EndFlush)(struct IMpegCodec *, u32);               /* 0x98 */
    _EQPErrors (*AddBuffer)(struct IMpegCodec *, u32, struct _QP_BUFFER_DESCRIPTOR *);      /* 0xA0 */
    _EQPErrors (*CancelBuffer)(struct IMpegCodec *, u32, struct _QP_BUFFER_DESCRIPTOR *);   /* 0xA8 */
    _EQPErrors (*TimeoutBuffer)(struct IMpegCodec *, u32, struct _QP_BUFFER_DESCRIPTOR *);  /* 0xB0 */
    _EQPErrors (*GetTime)(struct IMpegCodec *, u32, u64 *);         /* 0xB8 */
    _EQPErrors (*XferData)(struct IMpegCodec *, u32, struct _QP_BUFFER_DESCRIPTOR *);       /* 0xC0 */
};                                                                  /* total: 0xC8 */

struct CQLCodec {
    struct CObject m_Object;                    /* 0x00 */
    struct IMpegCodec m_iMpegCodec;             /* 0x38 */
    struct IHCIAPI m_hci;                       /* 0x100 */
    int m_bHCIInited;                           /* 0x2C0 */
    u32 _pad2C4;                                /* 0x2C4 */
    struct CUsbCntl *m_pUsbCntl;                /* 0x2C8 */
    struct CPCIeCntl *m_pPCIeCntl;              /* 0x2D0 */
    _EQPErrors (*m_pDeviceCallback)(void *, u32, void *); /* 0x2D8 */
    void *m_callbackContext;                    /* 0x2E0 */
    struct CTask *m_pTask;                      /* 0x2E8 */
    u8 *m_pVideoFW;                             /* 0x2F0 */
    u8 *m_pAudioFW;                             /* 0x2F8 */
    u32 m_QL201FWSize;                          /* 0x300 */
    u32 m_QL201AudFWSize;                       /* 0x304 */
    int m_bVideoFWUpdated;                      /* 0x308 */
    int m_bAudioFWUpdated;                      /* 0x30C */
    u16 m_ENC_REG_MESSAGE;                      /* 0x310 */
    u16 m_ENC_REG_SYSTEM_CONTROL;               /* 0x312 */
    u16 m_ENC_REG_PICTURE_RESOLUTION;           /* 0x314 */
    u16 m_ENC_REG_INPUT_CONTROL;                /* 0x316 */
    u16 m_ENC_REG_RATE_CONTROL;                 /* 0x318 */
    u16 m_ENC_REG_BIT_RATE;                     /* 0x31A */
    u16 m_ENC_REG_FILTER_CONTROL;               /* 0x31C */
    u16 m_ENC_REG_GOP_LOOP_FILTER;              /* 0x31E */
    u16 m_ENC_REG_ET_CONTROL;                   /* 0x320 */
    u16 m_ENC_REG_BLOCK_SIZE;                   /* 0x322 */
    u16 m_ENC_REG_OUT_PIC_RESOLUTION;           /* 0x324 */
    u16 m_ENC_REG_AUDIO_CONTROL_PARAM;          /* 0x326 */
    u16 m_ENC_REG_AUDIO_CONTROL_EX;             /* 0x328 */
    u16 _pad32A;                                /* 0x32A */
    u32 _pad32C;                                /* 0x32C */
    struct CObjectMgr *m_pChannelMgr;           /* 0x330 */
    int m_VOEnable;                             /* 0x338 */
    u32 m_VOUMode;                              /* 0x33C */
    u32 m_VOUStartPixel;                        /* 0x340 */
    u32 m_VOUStartLine;                         /* 0x344 */
    int m_AOEnable;                             /* 0x348 */
    u32 m_AOControls_ao_msb;                    /* 0x34C */
    u32 m_AOControls_lrclk_i;                   /* 0x350 */
    u32 m_AOControls_bclk_i;                    /* 0x354 */
    u32 m_AOControls_ao_i2s;                    /* 0x358 */
    u32 m_AOControls_ao_rj;                     /* 0x35C */
    u32 m_AOControls_ao_s;                      /* 0x360 */
    _DEC_AO_CONTROL_PARAM m_AOControls;         /* 0x364 */
    _DEC_AUDIO_CONTROL_PARAM m_AudControls;     /* 0x368 */
    u32 m_VIUMode;                              /* 0x36C */
    u32 m_VIUFormat;                            /* 0x370 */
    u32 m_VIUStartPixel;                        /* 0x374 */
    u32 m_VIUStartLine;                         /* 0x378 */
    u32 m_ClkEdge;                              /* 0x37C */
    u32 m_ulViuSyncCode1;                       /* 0x380 */
    u32 m_ulViuSyncCode2;                       /* 0x384 */
    u32 m_AIControls_ai_msb;                    /* 0x388 */
    u32 m_AIControls_lrclk_i;                   /* 0x38C */
    u32 m_AIControls_bclk_i;                    /* 0x390 */
    u32 m_AIControls_ai_i2s;                    /* 0x394 */
    u32 m_AIControls_ai_rj;                     /* 0x398 */
    u32 m_AIControls_ai_m;                      /* 0x39C */
    u32 m_ulEncFirmwareVer;                     /* 0x3A0 */
    u32 m_ulDecFirmwareVer;                     /* 0x3A4 */
    u32 m_ulSysFirmwareVer;                     /* 0x3A8 */
    u32 m_ulAudFirmwareVer;                     /* 0x3AC */
    int m_bInterruptAttached;                   /* 0x3B0 */
    u32 m_interruptNumber;                      /* 0x3B4 */
    void *m_isrID;                              /* 0x3B8 */
    _QPCODEC_MEM_TYPE m_MemType;                /* 0x3C0 */
    u32 m_MemSize;                              /* 0x3C4 */
    _QPCODEC_CHIP_TYPE m_ChipType;              /* 0x3C8 */
    u8 _pad3C9[3];                              /* 0x3C9 */
    _QLCODEC_CHIP_VERSION m_ChipVersion;        /* 0x3CC */
    u32 m_VerFwAPI;                             /* 0x3D0 */
    u32 m_FwIntMode;                            /* 0x3D4 */
    u32 m_FwFixedMode;                          /* 0x3D8 */
    u32 m_GPIODirections;                       /* 0x3DC */
    u32 m_GPIOValues;                           /* 0x3E0 */
    u32 _pad3E4;                                /* 0x3E4 */
    void *m_SemaphoreFWAPI;                     /* 0x3E8 */
    void *m_EvtTask;                            /* 0x3F0 */
    void *m_EvtTaskReply;                       /* 0x3F8 */
    void *m_EvtSyncDma;                         /* 0x400 */
    u32 m_State;                                /* 0x408 */
    u32 m_PowerState;                           /* 0x40C */
    int m_ErrorRecovery;                        /* 0x410 */
    u32 m_USBDefaultMode;                       /* 0x414 */
    u32 m_Pll4;                                 /* 0x418 */
    u32 m_Pll5;                                 /* 0x41C */
    u32 m_UseArtesaExt340;                      /* 0x420 */
    u32 m_AIVolume;                             /* 0x424 */
    u32 m_AOVolume;                             /* 0x428 */
    u32 _pad42C;                                /* 0x42C */
};                                              /* total: 0x430 */

#endif /* _CQLCODEC_H */
