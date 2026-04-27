/* include/abi/cdevice.h */
#ifndef _ABI_CDEVICE_H
#define _ABI_CDEVICE_H

#include <linux/types.h>
#include "qp_process_type.h"
#include "_ksdevice.h"

struct _KMUTANT;
struct AVer_DependMutex;
struct CCrossbarProperties;
struct _CM_RESOURCE_LIST;
struct _DMA_ADAPTER;
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
struct ProjectManager;
struct ProjectFactory;
struct _QPCODECLIB_INITDATA;
struct _QPCODEC_USB_BUS_DATA;

enum _YUV_PIN_STATE {
    YUV_PIN_DISCONNECTED = 0,
    YUV_PIN_CONNECTING = 1,
    YUV_PIN_CONNECTED = 2,
};

struct _DISPATCHER_HEADER {
    u8 Type;
    u8 Abandoned;
    u8 Size;
    u8 Hand;
    union {
        struct {
            u8 Inserted;
            u8 DebugActive;
            u8 DpcActive;
            u8 Reserved;
        };
        u32 Lock;
    };
    s32 SignalState;
    struct _LIST_ENTRY *WaitListHead;
    u8 _pad[8];
};

struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
};

struct _KTHREAD {
    u8 _data[1];
};

struct _KMUTANT {
    struct _DISPATCHER_HEADER Header;
    struct _LIST_ENTRY MutantListEntry;
    struct _KTHREAD *OwnerThread;
    u8 Abandoned;
    u8 ApcDisable;
    u8 _pad[6];
};

struct AVer_DependMutex {
    s64 _padding_;
    struct _KMUTANT m_dwMutex;
};

struct _QPCODECLIB_INITDATA {
    int bDownloadFW;
    u8 _pad04[4];
    u8 mpgCodecInitData[224];
    u8 vidDecoderInitData[12];
    u8 vidEncoderInitData[8];
    u8 tunerInitData[4];
    u8 tvAudioInitData[4];
    u8 audCodecInitData[4];
    u8 i2cInitData[12];
    u8 i2cInitDataEx[12];
};

struct _QPCODEC_USB_BUS_DATA {
    u32 dwPipeCmdWrite;
    u32 dwPipeCmdRead;
    u32 dwPipeDataWrite;
    u32 dwPipeDataRead;
};

struct _QP_TASK_HANDLE {
    u32 taskId;
    u8 _pad[4];
};

struct CDevice {
    s64 _padding_0x00;
    s64 _padding_0x08;
    s64 _padding_0x10;
    s64 _padding_0x18;
    s64 _padding_0x20;
    s64 _padding_0x28;
    s64 _padding_0x30;
    s64 _padding_0x38;
    s64 _padding_0x40;
    s64 _padding_0x48;
    s64 _padding_0x50;
    s64 _padding_0x58;
    s64 _padding_0x60;
    s64 _padding_0x68;
    enum QP_PROCESS_TYPE m_processname;
    struct _KMUTANT m_PL330Mutex;
    u32 m_dwFactoryDriver;
    struct AVer_DependMutex *m_pDeviceMutex;
    struct AVer_DependMutex m_PollingMutex;
    enum _YUV_PIN_STATE m_tYUVPinState;
    u32 m_dwEncoderOnly;
    u32 m_dwUSBproject;
    u32 m_dwHIUEncoderOnly;
    struct CCrossbarProperties *m_p_crossbar_properties;
    s64 m_rawAudRefTime;
    s64 m_rawAudRefDuration;
    int m_bSupportFME;
    int m_bSurprisedRemoved;
    struct _KSDEVICE *m_p_ks_dev;
    u32 m_medium_instance_id;
    struct _QPCODECLIB_INITDATA m_initData;
    struct _CM_RESOURCE_LIST *m_p_trans_res_list;
    struct _QPCODEC_USB_BUS_DATA m_usbBusData;
    struct _DMA_ADAPTER *m_p_dma_adapter;
    u8 m_PowerState;
    u8 _pad_281[7];
    struct ICodecLib *m_pCodecLib;
    struct IMpegCodec *m_pMpegCodec;
    struct IVideoDecoder *m_pVidDecoder;
    struct IVideoEncoder *m_pVidEncoder;
    struct ITuner *m_pTuner;
    struct ITVAudio *m_pTVAudio;
    struct IAudioCodec *m_pAudCodec;
    struct CPinManager *m_pPinsMgr;
    struct HAL *m_phal;
    struct CTaskEncode *m_pTask_Encode;
    struct CTaskRawVideo *m_pTask_RawVid;
    struct CTaskRawAudio *m_pTask_RawAud;
    struct AVer_GPIOI2C *m_pAVer_gpioi2c;
    struct CryptoAT88 *m_pAT88;
    u8 m_bCryptoInit;
    u8 _pad_2f9[3];
    struct _QP_TASK_HANDLE m_CurrentTask;
    struct ProjectManager *m_pProjectManager;
    struct ProjectFactory *m_pProjectFactory;
    u32 m_dwCapFilterExist;
    u32 m_dwEncFilterExist;
    int m_dwIsRunning;
    u8 m_FirstRun;
    u8 m_bGetSignalDone;
    u8 _pad_31e[2];
    u32 m_dwDetectCount;
    u32 m_dwDetectThreadHold;
};

static_assert(offsetof(struct CDevice, m_pTask_Encode) == 0x2E0, "CDevice layout mismatch!");

#endif /* ABI_CDEVICE_H */
