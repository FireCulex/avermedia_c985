/* SPDX-License-Identifier: GPL-2.0 */
/* cdevice.h — CDevice structure for AVerMedia C985 */
#ifndef CDEVICE_H
#define CDEVICE_H

#include <linux/types.h>

/* Forward declarations */
struct c_device;
struct _KMUTANT;
struct AVer_DependMutex;
struct _QPCODECLIB_INITDATA;
struct _QPCODEC_USB_BUS_DATA;
struct _KSDEVICE;
struct CCrossbarProperties;
struct _CM_RESOURCE_LIST;
struct _DMA_ADAPTER;
enum _DEVICE_POWER_STATE;
struct ICodecLib;
struct IMpegCodec;
struct IVideoDecoder;
struct IVideoEncoder;
struct ITuner;
struct ITVAudio;
struct IAudioCodec;
struct CPinManager;
struct HAL;
struct CTaskEncode;
struct CTaskRawVideo;
struct CTaskRawAudio;
struct AVer_GPIOI2C;
struct CryptoAT88;
struct _QP_TASK_HANDLE;
struct ProjectManager;
struct ProjectFactory;

/* ============================================
 * QP_PROCESS_TYPE Enum
 * ============================================ */
enum qp_process_type {
    QP_PROCESS_UNKNOWN = 0,
    QP_PROCESS_CAPTURE = 1,
    QP_PROCESS_ENCODER = 2,
    QP_PROCESS_DECODER = 3,
};

/* ============================================
 * _DISPATCHER_HEADER (24 bytes)
 * From Windows WDK
 * ============================================ */
struct _DISPATCHER_HEADER {
    u8 Type;                  /* 0x00 */
    u8 Abandoned;             /* 0x01 */
    u8 Size;                  /* 0x02 */
    u8 Hand;                  /* 0x03 */
    union {
        struct {
            u8 Inserted;      /* 0x04 */
            u8 DebugActive;   /* 0x05 */
            u8 DpcActive;     /* 0x06 */
            u8 Reserved;      /* 0x07 */
        };
        u32 Lock;             /* 0x04 */
    };
    s32 SignalState;          /* 0x08 */
    struct _LIST_ENTRY *WaitListHead; /* 0x0C */
    u8 _pad[8];               /* 0x10 - padding to 0x18 */
};

/* ============================================
 * _LIST_ENTRY (16 bytes)
 * From Windows WDK
 * ============================================ */
struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink; /* 0x00 */
    struct _LIST_ENTRY *Blink; /* 0x08 */
};

/* ============================================
 * _KTHREAD (opaque, 8 bytes pointer)
 * ============================================ */
struct _KTHREAD {
    u8 _data[1];  /* Opaque */
};

/* ============================================
 * _KMUTANT (56 bytes)
 * From Windows WDK
 * ============================================ */
struct _KMUTANT {
    struct _DISPATCHER_HEADER Header;     /* 0x00 */
    struct _LIST_ENTRY MutantListEntry;   /* 0x18 */
    struct _KTHREAD *OwnerThread;         /* 0x28 */
    u8 Abandoned;                         /* 0x30 */
    u8 ApcDisable;                        /* 0x31 */
    u8 _pad[6];                           /* 0x32 - padding to 0x38 */
};                                        /* Total: 0x38 = 56 bytes */

/* ============================================
 * AVer_DependMutex (64 bytes)
 * ============================================ */
struct AVer_DependMutex {
    s64 _padding;               /* 0x00 */
    struct _KMUTANT m_dwMutex;  /* 0x08 */
};                              /* Total: 0x40 = 64 bytes */

/* ============================================
 * _QPCODECLIB_INITDATA (288 bytes)
 * ============================================ */
struct _QPCODECLIB_INITDATA {
    int bDownloadFW;                        /* 0x00 */
    u8 _pad1[8];                            /* 0x04 */
    u8 mpgCodecInitData[224];               /* 0x08 - _QPMPGCODEC_INITDATA */
    u8 vidDecoderInitData[12];              /* 0xE8 - _QPVIDDECODER_INITDATA */
    u8 vidEncoderInitData[8];               /* 0xF4 - _QPVIDENCODER_INITDATA */
    u8 tunerInitData[4];                    /* 0xFC - _QPTUNER_INITDATA */
    u8 tvAudioInitData[4];                  /* 0x100 - _QPTVAUDIO_INITDATA */
    u8 audCodecInitData[4];                 /* 0x104 - _QPAUDCODEC_INITDATA */
    u8 i2cInitData[12];                     /* 0x108 - _QPI2C_INITDATA */
    u8 i2cInitDataEx[12];                   /* 0x114 - _QPI2C_INITDATA */
};                                          /* Total: 0x120 = 288 bytes */

/* ============================================
 * _QPCODEC_USB_BUS_DATA (16 bytes)
 * ============================================ */
struct _QPCODEC_USB_BUS_DATA {
    u32 dwPipeCmdWrite;     /* 0x00 */
    u32 dwPipeCmdRead;      /* 0x04 */
    u32 dwPipeDataWrite;    /* 0x08 */
    u32 dwPipeDataRead;     /* 0x0C */
};                          /* Total: 0x10 = 16 bytes */

/* ============================================
 * _YUV_PIN_STATE Enum
 * ============================================ */
enum _YUV_PIN_STATE {
    YUV_PIN_DISCONNECTED = 0,
    YUV_PIN_CONNECTING = 1,
    YUV_PIN_CONNECTED = 2,
};

/* ============================================
 * _DEVICE_POWER_STATE Enum
 * ============================================ */
enum _DEVICE_POWER_STATE {
    PowerDeviceUnspecified = 0,
    PowerDeviceD0 = 1,
    PowerDeviceD1 = 2,
    PowerDeviceD2 = 3,
    PowerDeviceD3 = 4,
    PowerDeviceMaximum = 5,
};

/* ============================================
 * _QP_TASK_HANDLE
 * ============================================ */
struct _QP_TASK_HANDLE {
    u32 taskId;             /* 0x00 */
    u8 _pad[4];             /* 0x04 */
};

/* ============================================
 * CDevice Structure
 * From Windows decompilation
 * ============================================ */
struct c_device {
    s64 _padding_0x00;                  /* 0x00 */
    s64 _padding_0x08;                  /* 0x08 */
    s64 _padding_0x10;                  /* 0x10 */
    s64 _padding_0x18;                  /* 0x18 */
    s64 _padding_0x20;                  /* 0x20 */
    s64 _padding_0x28;                  /* 0x28 */
    s64 _padding_0x30;                  /* 0x30 */
    s64 _padding_0x38;                  /* 0x38 */
    s64 _padding_0x40;                  /* 0x40 */
    s64 _padding_0x48;                  /* 0x48 */
    s64 _padding_0x50;                  /* 0x50 */
    s64 _padding_0x58;                  /* 0x58 */
    s64 _padding_0x60;                  /* 0x60 */
    s64 _padding_0x68;                  /* 0x68 */
    enum qp_process_type m_processname; /* 0x70 */
    u8 _pad1[4];                        /* 0x74 */
    struct _KMUTANT m_PL330Mutex;       /* 0x78 */
    u32 m_dwFactoryDriver;              /* 0xB0 */
    u8 _pad2[4];                        /* 0xB4 */
    struct AVer_DependMutex *m_pDeviceMutex; /* 0xB8 */
    struct AVer_DependMutex m_PollingMutex;  /* 0xC0 */
    enum _YUV_PIN_STATE m_tYUVPinState; /* 0x100 */
    u32 m_dwEncoderOnly;                /* 0x104 */
    u32 m_dwUSBproject;                 /* 0x108 */
    u32 m_dwHIUEncoderOnly;             /* 0x10C */
    struct CCrossbarProperties *m_p_crossbar_properties; /* 0x110 */
    s64 m_rawAudRefTime;                /* 0x118 */
    s64 m_rawAudRefDuration;            /* 0x120 */
    int m_bSupportFME;                  /* 0x128 */
    int m_bSurprisedRemoved;            /* 0x12C */
    struct _KSDEVICE *m_p_ks_dev;       /* 0x130 */
    u32 m_medium_instance_id;           /* 0x138 */
    u8 _pad3[4];                        /* 0x13C */
    struct _QPCODECLIB_INITDATA m_initData; /* 0x140 */
    struct _CM_RESOURCE_LIST *m_p_trans_res_list; /* 0x260 */
    struct _QPCODEC_USB_BUS_DATA m_usbBusData; /* 0x268 */
    struct _DMA_ADAPTER *m_p_dma_adapter; /* 0x278 */
    enum _DEVICE_POWER_STATE m_PowerState; /* 0x280 */
    u8 _pad4[7];                        /* 0x281 */
    struct ICodecLib *m_pCodecLib;      /* 0x288 */
    struct IMpegCodec *m_pMpegCodec;    /* 0x290 */
    struct IVideoDecoder *m_pVidDecoder; /* 0x298 */
    struct IVideoEncoder *m_pVidEncoder; /* 0x2A0 */
    struct ITuner *m_pTuner;            /* 0x2A8 */
    struct ITVAudio *m_pTVAudio;        /* 0x2B0 */
    struct IAudioCodec *m_pAudCodec;    /* 0x2B8 */
    struct CPinManager *m_pPinsMgr;     /* 0x2C0 */
    struct HAL *m_phal;                 /* 0x2C8 */
    struct CTaskEncode *m_pTask_Encode; /* 0x2D0 */
    struct CTaskRawVideo *m_pTask_RawVid; /* 0x2D8 */
    struct CTaskRawAudio *m_pTask_RawAud; /* 0x2E0 */
    struct AVer_GPIOI2C *m_pAVer_gpioi2c; /* 0x2E8 */
    struct CryptoAT88 *m_pAT88;         /* 0x2F0 */
    u8 m_bCryptoInit;                   /* 0x2F8 */
    u8 _pad5[3];                        /* 0x2F9 */
    struct _QP_TASK_HANDLE m_CurrentTask; /* 0x2FC */
    struct ProjectManager *m_pProjectManager; /* 0x300 */
    struct ProjectFactory *m_pProjectFactory; /* 0x308 */
    u32 m_dwCapFilterExist;             /* 0x310 */
    u32 m_dwEncFilterExist;             /* 0x314 */
    int m_dwIsRunning;                  /* 0x318 */
    u8 m_FirstRun;                      /* 0x31C */
    u8 m_bGetSignalDone;                /* 0x31D */
    u8 _pad6[2];                        /* 0x31E */
    u32 m_dwDetectCount;                /* 0x320 */
    u32 m_dwDetectThreadHold;           /* 0x324 */
};                                      /* Total: 0x328 bytes */

#endif /* CDEVICE_H */
