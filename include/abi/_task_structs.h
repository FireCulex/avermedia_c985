/* include/abi/task_structs.h */
#ifndef _TASK_STRUCTS_H
#define _TASK_STRUCTS_H

#include <linux/types.h>
#include "cobject.h"
#include "task_enums.h"
#include "qp_buffer_descriptor.h"

struct CChannel;
struct CFifo;

/* Register unions - all u32 with bitfield access */
typedef union _SYSTEM_CONTROL {
    u32 Read;
} _SYSTEM_CONTROL;

typedef union _PICTURE_RESOLUTION {
    u32 Read;
} _PICTURE_RESOLUTION;

typedef union _INPUT_CONTROL {
    u32 Read;
} _INPUT_CONTROL;

typedef union _RATE_CONTROL {
    u32 Read;
} _RATE_CONTROL;

typedef union _RATE_CONTROL_EX {
    u32 Read;
} _RATE_CONTROL_EX;

typedef union _BIT_RATE {
    u32 Read;
} _BIT_RATE;

typedef union _FILTER_CONTROL {
    u32 Read;
} _FILTER_CONTROL;

typedef union _GOP_LOOP_FILTER {
    u32 Read;
} _GOP_LOOP_FILTER;

typedef union _ET_CONTROL {
    u32 Read;
} _ET_CONTROL;

typedef union _BLOCK_XFER_SIZE {
    u32 Read;
} _BLOCK_XFER_SIZE;

typedef union _OUT_PICTURE_RESOLUTION {
    u32 Read;
} _OUT_PICTURE_RESOLUTION;

typedef union _AUDIO_CONTROL_PARAM {
    u32 Read;
} _AUDIO_CONTROL_PARAM;

typedef union _AUDIO_CONTROL_EX_AAC {
    u32 Read;
} _AUDIO_CONTROL_EX_AAC;

typedef union _AUDIO_CONTROL_EX_G711 {
    u32 Read;
} _AUDIO_CONTROL_EX_G711;

typedef union _AUDIO_CONTROL_EX_LPCM {
    u32 Read;
} _AUDIO_CONTROL_EX_LPCM;

typedef union _AUDIO_CONTROL_EX_SILK {
    u32 Read;
} _AUDIO_CONTROL_EX_SILK;

typedef union _DEC_STREAM_TYPE {
    u32 Read;
} _DEC_STREAM_TYPE;

typedef union _DEC_AO_CONTROL_PARAM {
    u32 Read;
} _DEC_AO_CONTROL_PARAM;

typedef union _DEC_AUDIO_CONTROL_PARAM {
    u32 Read;
} _DEC_AUDIO_CONTROL_PARAM;

typedef union _QPDEC_DISP_ATTRIB_XY {
    u32 Read;
} _QPDEC_DISP_ATTRIB_XY;

typedef union _QPCODEC_SYS_FUNCTION {
    u32 function;
} _QPCODEC_SYS_FUNCTION;

typedef union _QLCODEC_CHIP_VERSION {
    u32 Read;
} _QLCODEC_CHIP_VERSION;

struct _QPCODEC_AUDIO_ENHANCEMENT {
    u32 gain1;              /* 0x00 */
    u32 gain2;              /* 0x04 */
    u32 add;                /* 0x08 */
    u32 sub;                /* 0x0C */
    u32 att1;               /* 0x10 */
    u32 att2;               /* 0x14 */
    u32 lgain;              /* 0x18 */
    u32 rgain;              /* 0x1C */
    u32 reserved1;          /* 0x20 */
    u32 reserved2;          /* 0x24 */
};                          /* total: 0x28 */

struct _QPCODEC_ENC_CAP_MODE {
    u32 capMode;            /* 0x00 */
    u32 trigMode;           /* 0x04 */
    u32 gpioPin;            /* 0x08 */
    u32 reserved1;          /* 0x0C */
    u32 reserved2;          /* 0x10 */
};                          /* total: 0x14 */

struct _QPCODEC_EXTERNAL_TRIGGER_MODE {
    int Enable;             /* 0x00 */
    u32 gpioPin;            /* 0x04 */
    u32 reserved1;          /* 0x08 */
    u32 reserved2;          /* 0x0C */
};                          /* total: 0x10 */

struct _QPCODEC_PTS_COUNTER_RESET {
    int Enable;             /* 0x00 */
    u32 gpioPin;            /* 0x04 */
    int immediate;          /* 0x08 */
    u32 reserved1;          /* 0x0C */
    u32 reserved2;          /* 0x10 */
};                          /* total: 0x14 */

struct _QPCODEC_DECIMATION_FACTOR {
    u32 input_format;       /* 0x00 */
    u32 output_format;      /* 0x04 */
    u32 scale_factor;       /* 0x08 */
    u32 reserved1;          /* 0x0C */
    u32 reserved2;          /* 0x10 */
};                          /* total: 0x14 */

struct _TASK_DMA_REQUEST {
    u32 ulArmAddr;          /* 0x00 */
    u8 *pBuffer;            /* 0x04 */
    u32 ulLength;           /* 0x0C */
    u32 ulOffset;           /* 0x10 */
    _CHANNEL_DIRECTION dir; /* 0x14 */
    int bSwap;              /* 0x18 */
} __packed;                 /* total: 0x1C */

struct _TASK_USER_BUFFER {
    u8 _data[0x30];        /* placeholder */
};                          /* total: 0x30 */

struct _TASK_ARM_REQUEST {
    u8 _data[0x48];        /* placeholder */
};                          /* total: 0x48 */

struct _TASK_IO_PENDING {
    u8 _data[0x2C];        /* placeholder */
};                          /* total: 0x2C */

/* _TASK_DATA: 0x648 bytes */
struct _TASK_DATA {
    struct CObject m_Object;                                /* 0x00 */
    u32 id;                                                 /* 0x38 */
    int valid;                                              /* 0x3C */
    _TASK_TYPE type;                                        /* 0x40 */
    u32 m_dwSession;                                        /* 0x44 */
    u32 m_dwStarted;                                        /* 0x48 */
    u32 m_dwPaused;                                         /* 0x4C */
    int m_bAcquired;                                        /* 0x50 */
    _TASK_STATE m_State;                                    /* 0x54 */
    u8 m_Error;                                             /* 0x58 */
    u8 _pad59[3];                                           /* 0x59 */
    int m_StartID;                                          /* 0x5C */
    struct _TASK_USER_BUFFER UserBuffer[7];                 /* 0x60 */
    struct _TASK_ARM_REQUEST ArmRequest[7];                 /* 0x1B0 */
    int ArmRequestNumber[7];                                /* 0x3A8 */
    u32 ArmBufferAddr;                                      /* 0x3C4 */
    struct _QP_BUFFER_DESCRIPTOR *pBufDescToCancel[7];      /* 0x3C8 */
    int bFlushing[7];                                       /* 0x400 */
    struct CChannel *pChannel[7];                           /* 0x41C */
    _CHANNEL_DIRECTION direction[7];                        /* 0x454 */
    _FIRMWARE_DATA_TYPE FWDataType[7];                      /* 0x470 */
    struct CFifo *pArmMsgFifo[7];                           /* 0x48C */
    int bDone;                                              /* 0x4C4 */
    _SYSTEM_CONTROL m_systemControl;                        /* 0x4C8 */
    _PICTURE_RESOLUTION m_pictureResolution;                /* 0x4CC */
    _INPUT_CONTROL m_inputControl;                          /* 0x4D0 */
    u32 m_syncMode;                                         /* 0x4D4 */
    _RATE_CONTROL m_rateControl;                            /* 0x4D8 */
    _RATE_CONTROL_EX m_rateControlEx;                       /* 0x4DC */
    _BIT_RATE m_bitRate;                                    /* 0x4E0 */
    _FILTER_CONTROL m_filterControl;                        /* 0x4E4 */
    _GOP_LOOP_FILTER m_gopLoopFilter;                       /* 0x4E8 */
    _ET_CONTROL m_etControl;                                /* 0x4EC */
    _BLOCK_XFER_SIZE m_blkXferSize;                         /* 0x4F0 */
    _OUT_PICTURE_RESOLUTION m_outPictureResolution;         /* 0x4F4 */
    _AUDIO_CONTROL_PARAM m_audioControlParam;               /* 0x4F8 */
    _AUDIO_CONTROL_EX_AAC m_audioControlExAAC;              /* 0x4FC */
    _AUDIO_CONTROL_EX_G711 m_audioControlExG711;            /* 0x500 */
    _AUDIO_CONTROL_EX_LPCM m_audioControlExLPCM;           /* 0x504 */
    _AUDIO_CONTROL_EX_SILK m_audioControlExSILK;            /* 0x508 */
    u32 m_EncIndexCapFreq;                                  /* 0x50C */
    u32 m_MP4VideoBlockNumber;                              /* 0x510 */
    struct _QPCODEC_AUDIO_ENHANCEMENT m_audioEnhancement;   /* 0x514 */
    u32 m_EncStopMode;                                      /* 0x53C */
    int m_bEncEnableVidPadding;                             /* 0x540 */
    int m_bEncVidFrozen;                                    /* 0x544 */
    int m_bEncVidStillInput;                                /* 0x548 */
    struct _QPCODEC_ENC_CAP_MODE m_EncCapMode;             /* 0x54C */
    u32 m_EncMjpegQuality;                                  /* 0x560 */
    u32 m_EncMjpegFrameBuffer;                              /* 0x564 */
    struct _QPCODEC_EXTERNAL_TRIGGER_MODE m_ExternalTriggerToSync; /* 0x568 */
    struct _QPCODEC_PTS_COUNTER_RESET m_PTSCounterReset;   /* 0x578 */
    struct _QPCODEC_DECIMATION_FACTOR m_RawVideoDecimationFactor; /* 0x58C */
    u32 m_DeinterlaceMode;                                  /* 0x5A0 */
    u32 m_LargeCompressBufferControl;                       /* 0x5A4 */
    u32 m_EnableLowBitrateMode;                             /* 0x5A8 */
    u32 m_encFontTableAddr;                                 /* 0x5AC */
    u32 m_encTextListAddr;                                  /* 0x5B0 */
    u32 m_encSyncTimeAddr;                                  /* 0x5B4 */
    u32 m_encVidBufLumaAddr;                                /* 0x5B8 */
    u32 m_encVidBufChromaAddr;                              /* 0x5BC */
    u32 m_encFrameLABufAddr;                                /* 0x5C0 */
    u32 m_encTopLABufAddr;                                  /* 0x5C4 */
    u32 m_encBottomLABufAddr;                               /* 0x5C8 */
    u32 m_encVidBufPTS;                                     /* 0x5CC */
    _DEC_STREAM_TYPE m_streamID;                            /* 0x5D0 */
    u32 m_outputMode;                                       /* 0x5D4 */
    _PICTURE_RESOLUTION m_decodeResolution;                 /* 0x5D8 */
    u32 m_decStopMode;                                      /* 0x5DC */
    u32 m_decStopDispMode;                                  /* 0x5E0 */
    u32 m_decPauseDispMode;                                 /* 0x5E4 */
    u32 m_decTrickPlaySpeed;                                /* 0x5E8 */
    u32 m_decTrickPlayDirection;                            /* 0x5EC */
    u32 m_decTrickFieldPreference;                          /* 0x5F0 */
    u32 m_decDispMode;                                      /* 0x5F4 */
    u32 m_decDispTVSystem;                                  /* 0x5F8 */
    _QPDEC_DISP_ATTRIB_XY m_decDispInputSize;              /* 0x5FC */
    _QPDEC_DISP_ATTRIB_XY m_decDispOutputSize;             /* 0x600 */
    _QPDEC_DISP_ATTRIB_XY m_decDispInputOrigin;            /* 0x604 */
    _QPDEC_DISP_ATTRIB_XY m_decDispOutputOrigin;           /* 0x608 */
    u32 m_decFlushBufSelect;                                /* 0x60C */
    u32 m_decXferMode;                                      /* 0x610 */
    u32 m_decXferMinimumSize;                               /* 0x614 */
    u32 m_decFWSharedMemWr;                                 /* 0x618 */
    u32 m_decFWSharedMemRd;                                 /* 0x61C */
    _QPCODEC_SYS_FUNCTION m_codec_function;                 /* 0x620 */
    u32 m_video_input;                                      /* 0x624 */
    u32 m_video_in_ch;                                      /* 0x628 */
    u32 m_video_output;                                     /* 0x62C */
    u32 m_video_out_ch;                                     /* 0x630 */
    u32 m_audio_input;                                      /* 0x634 */
    u32 m_audio_in_ch;                                      /* 0x638 */
    u32 m_audio_output;                                     /* 0x63C */
    u32 m_audio_out_ch;                                     /* 0x640 */
    void *m_EvtReply;                                       /* 0x644 */
};                                                          /* total: 0x64C */

#endif /* _TASK_STRUCTS_H */
