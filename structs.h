/*
 * ============================================================================
 * WARNING: DO NOT MODIFY THIS FILE WITHOUT EXPLICIT PERMISSION
 * ============================================================================
 * This file contains the core device structures for the AVerMedia C985 driver.
 * All structures are based on Windows PDB decompilation and must match exactly.
 *
 * Before making ANY changes:
 * 1. Verify against Ghidra/IDA decompilation
 * 2. Confirm structure offsets match Windows driver
 * 3. Get explicit confirmation before modifying structure layouts
 *
 * Unauthorized modifications WILL cause driver crashes and data corruption.
 * ============================================================================
 */
/* SPDX-License-Identifier: GPL-2.0 */

#ifndef C985_STRUCTS_H
#define C985_STRUCTS_H

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

#include "types.h"

/* ============================================
 * ARM Buffer Variants (56 bytes each)
 * ============================================ */

struct arm_buffer_all {
    u32 valid;                  /* 0x00 */
    u32 reserved1;              /* 0x04 */
    u32 reserved2;              /* 0x08 */
    u32 reserved3;              /* 0x0C */
    u32 ulPTS;                  /* 0x10 */
    u32 reserved4;              /* 0x14 */
    u32 reserved5;              /* 0x18 */
    u32 reserved6;              /* 0x1C */
    u32 ulPTSValid;             /* 0x20 */
    u32 ulDataType;             /* 0x24 */
    u32 reserved7;              /* 0x28 */
    u32 reserved8;              /* 0x2C */
    u32 reserved9;              /* 0x30 */
    u32 ulFwData;               /* 0x34 */
} __packed;

struct arm_buffer_yuv {
    u32 ulYAddr;                /* 0x00 */
    u32 ulUVAddr;               /* 0x04 */
    u32 reserved1;              /* 0x08 */
    u32 ulSize;                 /* 0x0C */
    u32 ulPTS;                  /* 0x10 */
    u32 ulYOffset;              /* 0x14 */
    u32 ulUVOffset;             /* 0x18 */
    u32 reserved2;              /* 0x1C */
    u32 ulPTSValid;             /* 0x20 */
    u32 ulDataType;             /* 0x24 */
    u32 ulLast;                 /* 0x28 */
    u32 ulWidth;                /* 0x2C */
    u32 ulHeight;               /* 0x30 */
    u32 ulFwData;               /* 0x34 */
};

struct arm_buffer_others {
    u32 ulAddr;                 /* 0x00 */
    u32 reserved1;              /* 0x04 */
    u32 reserved2;              /* 0x08 */
    u32 ulSize;                 /* 0x0C */
    u32 ulPTS;                  /* 0x10 */
    u32 ulOffset;               /* 0x14 */
    u32 reserved3;              /* 0x18 */
    u32 ulCompressAudioType;    /* 0x1C */
    u32 ulPTSValid;             /* 0x20 */
    u32 ulDataType;             /* 0x24 */
    u32 ulLast;                 /* 0x28 */
    u32 ulFrameFlags;           /* 0x2C */
    u32 ulWrapAround;           /* 0x30 */
    u32 ulFwData;               /* 0x34 */
};

union arm_buffer_union {
    struct arm_buffer_all ALL;
    struct arm_buffer_yuv YUV;
    struct arm_buffer_others OTHERS;
};

/* ============================================
 * Task ARM Buffer (0x3C = 60 bytes)
 * ============================================ */
struct task_arm_buffer {
    union arm_buffer_union BUFFER;          /* 0x00 */
    enum arm_buffer_type type;              /* 0x38 */
};

/* ============================================
 * User Buffer Variants (36 bytes each)
 * ============================================ */

struct user_buffer_yuv {
    u8 *pYBuffer;               /* 0x00 */
    u32 ulYLength;              /* 0x08 */
    u8 *pUVBuffer;              /* 0x0C */
    u32 ulUVLength;             /* 0x14 */
    u8 *reserved1;              /* 0x18 */
    u32 reserved2;              /* 0x20 */
} __packed;

struct user_buffer_others {
    u8 *pBuffer;                /* 0x00 */
    u32 ulLength;               /* 0x08 */
    void *reserved1;            /* 0x0C */
    u32 reserved2;              /* 0x14 */
    void *reserved3;            /* 0x18 */
    u32 reserved4;              /* 0x20 */
} __packed;

union user_buffer_union {
    struct user_buffer_yuv YUV;
    struct user_buffer_others OTHERS;
};

/* ============================================
 * Task User Buffer (0x30 = 48 bytes)
 * ============================================ */
struct task_user_buffer {
    union user_buffer_union BUFFER;     /* 0x00 - 36 bytes */
    u8 status;                          /* 0x24 - 1 byte */
    u8 _pad1[3];                        /* 0x25 - 3 bytes padding */
    struct qp_buffer_descriptor *pBufDesc;  /* 0x28 - 8 bytes */
} __packed;                             /* ← Keep this (48 bytes total) */

/* ============================================
 * Task ARM Request (0x48 = 72 bytes)
 * ============================================ */
struct task_arm_request {
    struct task_arm_buffer ArmBuffer;       /* 0x00 */
    struct host_message HostMsg;            /* 0x3C */
    struct host_message_status HostMsg_status; /* 0x40 */
    u32 reqId;                              /* 0x44 */
} __packed;

/* ============================================
 * CObject (0x38 = 56 bytes)
 * ============================================ */
struct c_object {
    int (*Init)(struct c_object *self);   /* 0x00 */
    int (*Done)(struct c_object *self);   /* 0x08 */
    struct c_object *m_pParent;         /* 0x10 */
    int m_fInitialized;                 /* 0x18 */
    u32 m_dwObjectAttributes;           /* 0x1C */
    void *m_semCriticalSection;         /* 0x20 */
    spinlock_t m_spinlock;              /* 0x28 - Linux: 4 bytes */
    u8 _pad_spin[4];                    /* 0x2C - PADDING: Windows m_spinlock is ulong64 (8 bytes) */
    u8 m_irql;                          /* 0x30 */
    u8 _pad[7];                         /* 0x31 - Pad to 0x38 */
};

struct c_object_entry {
    void *pObject;                      /* Pointer to the managed object */
    u32 hObject;                        /* Handle ID */
    u32 _pad;                           /* Padding for 8-byte alignment */
    struct c_object_entry *pNext;       /* Next entry in list */
};

struct c_object_mgr {
    struct c_object m_Object;           /* 0x00 - Base class */
    u32 m_hCurObject;                   /* 0x38 */
    u32 _pad1;                          /* 0x3C - Padding for pointer alignment */
    struct c_object_entry *m_pHead;     /* 0x40 */
    u32 m_dwObjectNb;                   /* 0x48 */
    u32 _pad2;                          /* 0x4C - Padding for pointer alignment */
    void *m_pFuncCallback;              /* 0x50 - _EQPErrors (*)(void *) */
};                                      /* Size: 0x58 (88 bytes) */


/* Queue entry - 16 bytes */
struct queue_entry {
    void *Data;                     /* 0x00 */
    struct queue_entry *pNext;      /* 0x08 */
};

/* ============================================
 * CFifo (0x54 = 84 bytes + spinlock)
 * ============================================ */
struct c_fifo {
    struct c_object m_Object;               /* 0x00 */
    u32 m_dwReadPtr;                        /* 0x38 */
    u32 m_dwWritePtr;                       /* 0x3C */
    u8 *m_Fifo;                             /* 0x40 */
    u32 m_dwFifoLevel;                      /* 0x48 */
    u32 m_size;                             /* 0x4C */
    u32 m_sizeEntry;                        /* 0x50 */
    spinlock_t lock;                        /* Linux addition */
};

/* ============================================
 * CChannel (0x1190 bytes)
 * ============================================ */
struct c_channel {
    struct c_object m_Object;               /* 0x000 */
    void *Open;                             /* 0x038 */
    void *Close;                            /* 0x040 */
    void *Start;                            /* 0x048 */
    void *Stop;                             /* 0x050 */
    void *Acquire;                          /* 0x058 */
    void *Pause;                            /* 0x060 */
    void *Step;                             /* 0x068 */
    void *SetRate;                          /* 0x070 */
    void *GetRate;                          /* 0x078 */
    void *BeginFlush;                       /* 0x080 */
    void *Flush;                            /* 0x088 */
    void *EndFlush;                         /* 0x090 */
    void *AddBuffer;                        /* 0x098 */
    void *CancelBuffer;                     /* 0x0A0 */
    void *TimeoutBuffer;                    /* 0x0A8 */
    void *XferData;                         /* 0x0B0 */
    void *GetBuffer;                        /* 0x0B8 */
    void *GetBufferYUV;                     /* 0x0C0 */
    void *GetBufferYUVRAS;                  /* 0x0C8 */
    void *CompleteBuffer;                   /* 0x0D0 */
    void *BeginChannelChange;               /* 0x0D8 */
    void *EndChannelChange;                 /* 0x0E0 */
    void *GetResolution;                    /* 0x0E8 */
    void *GetYUVFormat;                     /* 0x0F0 */
    u32 m_dwOpenFlags;                      /* 0x0F8 */
    u8 _pad1[4];                            /* 0x0FC */
    void (*m_pDeviceCallback)(struct c_channel *channel, u32 event,
                              struct qp_buffer_descriptor *desc, void *context);  /* 0x100 */
    void *m_callbackContext;                /* 0x108 */
    u32 m_hTask;                            /* 0x110 */
    u32 m_hChannel;                         /* 0x114 */
    int m_dataType;                         /* 0x118 */
    int m_FWDataType;                       /* 0x11C */
    enum channel_direction m_ChannelDirection; /* 0x120 */
    u32 m_ChannelType;                      /* 0x124 */
    struct c_task *m_pTask;                 /* 0x128 */
    int m_bOpened;                          /* 0x130 */
    u32 m_State;                            /* 0x134 */
    int m_bPaused;                          /* 0x138 */
    u8 _pad2[4];                            /* 0x13C */
    struct queue_entry m_Entries[256];      /* 0x140 - 256 * 0x10 = 0x1000 */
    struct c_queue *m_pFreeQueue;           /* 0x1140 */
    struct c_queue *m_pDataRequestQueue;    /* 0x1148 */
    struct c_queue *m_pDataPendingQueue;    /* 0x1150 */
    u64 m_llStartTime;                      /* 0x1158 */
    int m_bFlushing;                        /* 0x1160 */
    int m_bByteSwap;                        /* 0x1164 */
    u64 m_ullCntBytes;                      /* 0x1168 */
    u8 m_savedDword[4];                     /* 0x1170 */
    u32 m_savedNumberBytes;                 /* 0x1174 */
    s64 m_llBasePts;                        /* 0x1178 */
    s64 m_llPrevPts;                        /* 0x1180 */
    s64 m_llAddPts;                         /* 0x1188 */
};

/* ============================================
 * TEventBlock (0x318 bytes)
 * ============================================ */
struct t_event_block {
    int check;                              /* 0x00 */
    int bits;                               /* 0x04 */
    void *mutexID;                          /* 0x08 */
    struct completion events[32];           /* 0x10 */
    u64 spinlock;                           /* 0x310 */
};

/* ============================================
 * CThread (0x60 = 96 bytes)
 * ============================================ */
struct c_thread {
    void *m_ThreadProc;                     /* 0x00 */
    void *ThreadExit;                       /* 0x08 */
    void *m_threadID;                       /* 0x10 */
    void *m_context;                        /* 0x18 */
    u32 m_priority;                         /* 0x20 */
    u8 _pad1[4];                            /* 0x24 */
    void *m_EvtWait;                        /* 0x28 */
    void *m_EvtReply;                       /* 0x30 */
    int m_bRemoveUserSpaceMapping;          /* 0x38 */
    char m_szThreadName[32];                /* 0x3C */
    u8 _pad2[4];                            /* 0x5C */
};

/* ============================================
 * IMpegCodec (0xC8 = 200 bytes)
 * ============================================ */
struct i_mpeg_codec {
    void *InitDevice;                       /* 0x00 */
    void *Release;                          /* 0x08 */
    void *Reset;                            /* 0x10 */
    void *Set;                              /* 0x18 */
    void *Get;                              /* 0x20 */
    void *AllocEncodeTask;                  /* 0x28 */
    void *AllocDecodeTask;                  /* 0x30 */
    void *ReleaseTask;                      /* 0x38 */
    void *Open;                             /* 0x40 */
    void *Close;                            /* 0x48 */
    void *Start;                            /* 0x50 */
    void *Stop;                             /* 0x58 */
    void *Acquire;                          /* 0x60 */
    void *Pause;                            /* 0x68 */
    void *Step;                             /* 0x70 */
    void *SetRate;                          /* 0x78 */
    void *GetRate;                          /* 0x80 */
    void *BeginFlush;                       /* 0x88 */
    void *Flush;                            /* 0x90 */
    void *EndFlush;                         /* 0x98 */
    void *AddBuffer;                        /* 0xA0 */
    void *CancelBuffer;                     /* 0xA8 */
    void *TimeoutBuffer;                    /* 0xB0 */
    void *GetTime;                          /* 0xB8 */
    void *XferData;                         /* 0xC0 */
};

/* ============================================
 * IHCIAPI (0x1C0 = 448 bytes)
 * ============================================ */
struct ihciapi {
    struct c_object m_Object;               /* 0x00 */
    struct c_thread m_Thread;               /* 0x38 */

    void *ReadHciRegister;                  /* 0x98 */
    void *WriteHciRegister;                 /* 0xA0 */
    void *RegisterRead;                     /* 0xA8 */
    void *RegisterWrite;                    /* 0xB0 */
    void *RegisterReadEx;                   /* 0xB8 */
    void *RegisterWriteEx;                  /* 0xC0 */
    void *MemoryRead;                       /* 0xC8 */
    void *MemoryReadEx;                     /* 0xD0 */
    void *MemoryWrite;                      /* 0xD8 */
    void *StartDMAWrite;                    /* 0xE0 */
    void *StartDMARead;                     /* 0xE8 */
    void *ResetArm;                         /* 0xF0 */
    void *SetInterrupt;                     /* 0xF8 */
    void *ClearInterrupt;                   /* 0x100 */
    void *EnableInterrupts;                 /* 0x108 */
    void *DisableInterrupts;                /* 0x110 */
    void *GetInterruptsStatus;              /* 0x118 */
    void *GetInterruptMask;                 /* 0x120 */
    void *SetInterruptMask;                 /* 0x128 */
    void *CopyFromCommonBuffer;             /* 0x130 */
    void *DMAReadDone;                      /* 0x138 */
    void *DMAWriteDone;                     /* 0x140 */
    void *DMAXferDone;                      /* 0x148 */

    struct cql_codec *m_pMpegCodec;         /* 0x150 */
    enum qphci_mode m_access_mode;          /* 0x158 */
    enum qphci_bus m_bus_type;              /* 0x15C */
    u8 *m_pRegisterBase;                    /* 0x160 */
    u8 *m_pMemoryBase;                      /* 0x168 */
    u32 m_PageSize;                         /* 0x170 */
    u32 m_mem_mapping_start_addr[3];        /* 0x174 */
    u32 m_mem_mapping_end_addr[3];          /* 0x180 */
    u32 m_mem_mapping_offset[3];            /* 0x18C */
    void *m_EvtEmuTask;                     /* 0x198 */
    void *m_EvtEmuTaskReply;                /* 0x1A0 */
    void *m_pUsbCntl;                       /* 0x1A8 */
    void *m_pPCIeCntl;                      /* 0x1B0 */
    int m_bEmulationMode;                   /* 0x1B8 */
    u32 m_ulChipVer;                        /* 0x1BC */
};

/* ============================================
 * Task IO Pending (0x2C = 44 bytes)
 * ============================================ */
struct task_io_pending {
    u32 ulArmAddress;                       /* 0x00 */
    u8 *pHostAddress;                       /* 0x04 - NO padding */
    u32 ulLength;                           /* 0x0C */
    u32 ulXferMode;                         /* 0x10 */
    u32 ulPicWidth;                         /* 0x14 */
    u32 ulPicHeight;                        /* 0x18 */
    u8 status;                              /* 0x1C */
    u8 _pad1[3];                            /* 0x1D */
    void *pTaskData;                        /* 0x20 */
    u32 dataType;                           /* 0x28 */
} __packed;                                 /* Total: 44 bytes (0x2C) */

/* ============================================
 * Task Data (0x64C = 1612 bytes)
 * ============================================ */
struct task_data {
    struct c_object m_Object;               /* 0x00 */
    u32 id;                                 /* 0x38 */
    int valid;                              /* 0x3C */
    u32 type;                               /* 0x40 */
    u32 m_dwSession;                        /* 0x44 */
    u32 m_dwStarted;                        /* 0x48 */
    u32 m_dwPaused;                         /* 0x4C */
    int m_bAcquired;                        /* 0x50 */
    enum task_state m_State;                /* 0x54 */
    u8 m_Error;                             /* 0x58 */
    u8 _pad1[3];                            /* 0x59 */
    int m_StartID;                          /* 0x5C */
    struct task_user_buffer UserBuffer[7];  /* 0x60 - 7 * 0x30 = 0x150 */
    struct task_arm_request ArmRequest[7];  /* 0x1B0 - 7 * 0x48 = 0x1F8 */
    int ArmRequestNumber[7];                /* 0x3A8 */
    u32 ArmBufferAddr;                      /* 0x3C4 */
    void *pBufDescToCancel[7];              /* 0x3C8 */
    int bFlushing[7];                       /* 0x400 */
    void *pChannel[7];                      /* 0x41C */
    u32 direction[7];                       /* 0x454 */
    u32 FWDataType[7];                      /* 0x470 */
    void *pArmMsgFifo[7];                   /* 0x48C */
    int bDone;                              /* 0x4C4 */

    /* Encoder parameters */
    u32 m_systemControl;                    /* 0x4C8 */
    u32 m_pictureResolution;                /* 0x4CC */
    u32 m_inputControl;                     /* 0x4D0 */
    u32 m_syncMode;                         /* 0x4D4 */
    u32 m_rateControl;                      /* 0x4D8 */
    u32 m_rateControlEx;                    /* 0x4DC */
    u32 m_bitRate;                          /* 0x4E0 */
    u32 m_filterControl;                    /* 0x4E4 */
    u32 m_gopLoopFilter;                    /* 0x4E8 */
    u32 m_etControl;                        /* 0x4EC */
    u32 m_blkXferSize;                      /* 0x4F0 */
    u32 m_outPictureResolution;             /* 0x4F4 */
    u32 m_audioControlParam;                /* 0x4F8 */
    u32 m_audioControlExAAC;                /* 0x4FC */
    u32 m_audioControlExG711;               /* 0x500 */
    u32 m_audioControlExLPCM;               /* 0x504 */
    u32 m_audioControlExSILK;               /* 0x508 */
    u32 m_EncIndexCapFreq;                  /* 0x50C */
    u32 m_MP4VideoBlockNumber;              /* 0x510 */
    u8 m_audioEnhancement[0x28];            /* 0x514 */
    u32 m_EncStopMode;                      /* 0x53C */
    int m_bEncEnableVidPadding;             /* 0x540 */
    int m_bEncVidFrozen;                    /* 0x544 */
    int m_bEncVidStillInput;                /* 0x548 */
    u8 m_EncCapMode[0x14];                  /* 0x54C */
    u32 m_EncMjpegQuality;                  /* 0x560 */
    u32 m_EncMjpegFrameBuffer;              /* 0x564 */
    u8 m_ExternalTriggerToSync[0x10];       /* 0x568 */
    u8 m_PTSCounterReset[0x14];             /* 0x578 */
    u8 m_RawVideoDecimationFactor[0x14];    /* 0x58C */
    u32 m_DeinterlaceMode;                  /* 0x5A0 */
    u32 m_LargeCompressBufferControl;       /* 0x5A4 */
    u32 m_EnableLowBitrateMode;             /* 0x5A8 */

    /* Encoder buffer addresses */
    u32 m_encFontTableAddr;                 /* 0x5AC */
    u32 m_encTextListAddr;                  /* 0x5B0 */
    u32 m_encSyncTimeAddr;                  /* 0x5B4 */
    u32 m_encVidBufLumaAddr;                /* 0x5B8 */
    u32 m_encVidBufChromaAddr;              /* 0x5BC */
    u32 m_encFrameLABufAddr;                /* 0x5C0 */
    u32 m_encTopLABufAddr;                  /* 0x5C4 */
    u32 m_encBottomLABufAddr;               /* 0x5C8 */
    u32 m_encVidBufPTS;                     /* 0x5CC */

    /* Decoder parameters */
    u32 m_streamID;                         /* 0x5D0 */
    u32 m_outputMode;                       /* 0x5D4 */
    u32 m_decodeResolution;                 /* 0x5D8 */
    u32 m_decStopMode;                      /* 0x5DC */
    u32 m_decStopDispMode;                  /* 0x5E0 */
    u32 m_decPauseDispMode;                 /* 0x5E4 */
    u32 m_decTrickPlaySpeed;                /* 0x5E8 */
    u32 m_decTrickPlayDirection;            /* 0x5EC */
    u32 m_decTrickFieldPreference;          /* 0x5F0 */
    u32 m_decDispMode;                      /* 0x5F4 */
    u32 m_decDispTVSystem;                  /* 0x5F8 */
    u32 m_decDispInputSize;                 /* 0x5FC */
    u32 m_decDispOutputSize;                /* 0x600 */
    u32 m_decDispInputOrigin;               /* 0x604 */
    u32 m_decDispOutputOrigin;              /* 0x608 */
    u32 m_decFlushBufSelect;                /* 0x60C */
    u32 m_decXferMode;                      /* 0x610 */
    u32 m_decXferMinimumSize;               /* 0x614 */
    u32 m_decFWSharedMemWr;                 /* 0x618 */
    u32 m_decFWSharedMemRd;                 /* 0x61C */

    /* Link configuration */
    u32 m_codec_function;                   /* 0x620 */
    u32 m_video_input;                      /* 0x624 */
    u32 m_video_in_ch;                      /* 0x628 */
    u32 m_video_output;                     /* 0x62C */
    u32 m_video_out_ch;                     /* 0x630 */
    u32 m_audio_input;                      /* 0x634 */
    u32 m_audio_in_ch;                      /* 0x638 */
    u32 m_audio_output;                     /* 0x63C */
    u32 m_audio_out_ch;                     /* 0x640 */
    struct t_event_block *m_EvtReply;                       /* 0x644 */
} __packed;

/* ============================================
 * CTask (0x3498 bytes)
 * ============================================ */
struct c_task {
    struct c_object m_Object;               /* 0x00 */
    struct c_thread m_Thread;               /* 0x38 */

    void *Alloc;                            /* 0x98 */
    void *Release;                          /* 0xA0 */
    void *Open;                             /* 0xA8 */
    void *Close;                            /* 0xB0 */
    void *Start;                            /* 0xB8 */
    void *Stop;                             /* 0xC0 */
    void *Acquire;                          /* 0xC8 */
    void *Pause;                            /* 0xD0 */
    void *Resume;                           /* 0xD8 */
    void *CancelBuffer;                     /* 0xE0 */
    void *NewBuffer;                        /* 0xE8 */
    void *Flush;                            /* 0xF0 */
    void *FlushArm;                         /* 0xF8 */
    void *DMARequest;                       /* 0x100 */
    void *CompleteData;                     /* 0x108 */
    void *CompleteUser;                     /* 0x110 */
    void *CompleteArm;                      /* 0x118 */
    void *ProcessArmMessage;                /* 0x120 */
    void *ProcessIoComplete;                /* 0x128 */
    void *ProcessDataStreaming;             /* 0x130 */
    void *ProcessCancelBuffer;              /* 0x138 */
    void *ProcessFlush;                     /* 0x140 */
    void *ProcessFlushArm;                  /* 0x148 */

    struct cql_codec *m_pMpegCodec;         /* 0x150 */
    struct task_data m_TaskData[8];         /* 0x158 - 8 * 0x64C = 0x3260 */
    struct task_dma_request m_dmaRequest;   /* 0x33B8 */
    u8 _pad1[4];                            /* 0x33D4 */
    struct task_dma_request *m_pDmaRequest; /* 0x33D8 */
    struct t_event_block *m_EvtDmaReqComplete;              /* 0x33E0 */
    struct task_io_pending m_ioPending[2];  /* 0x33E8 */
    struct task_io_pending *m_pPending[2];  /* 0x3440 */
    struct c_object m_CritSectionIOPending; /* 0x3450 */
    u32 m_dwMaxDMASize;                     /* 0x3488 */
    int m_StartID;                          /* 0x348C */
    u32 m_taskIdPCMOut;                     /* 0x3490 */
    u32 m_taskIdAESOut;                     /* 0x3494 */
} __packed;

/* ============================================
 * CQLCodec (0x42C bytes)
 * ============================================ */
struct cql_codec {
    struct c_object m_Object;               /* 0x000 */
    struct i_mpeg_codec m_iMpegCodec;       /* 0x038 */
    struct ihciapi m_hci;                   /* 0x100 */

    int m_bHCIInited;                       /* 0x2C0 */
    u8 _pad1[4];                            /* 0x2C4 */
    void *m_pUsbCntl;                       /* 0x2C8 */
    void *m_pPCIeCntl;                      /* 0x2D0 */
    void *m_pDeviceCallback;                /* 0x2D8 */
    void *m_callbackContext;                /* 0x2E0 */
    struct c_task *m_pTask;                 /* 0x2E8 */
    u8 *m_pVideoFW;                         /* 0x2F0 */
    u8 *m_pAudioFW;                         /* 0x2F8 */
    u32 m_QL201FWSize;                      /* 0x300 */
    u32 m_QL201AudFWSize;                   /* 0x304 */
    int m_bVideoFWUpdated;                  /* 0x308 */
    int m_bAudioFWUpdated;                  /* 0x30C */

    /* Encoder register cache */
    u16 m_ENC_REG_MESSAGE;                  /* 0x310 */
    u16 m_ENC_REG_SYSTEM_CONTROL;           /* 0x312 */
    u16 m_ENC_REG_PICTURE_RESOLUTION;       /* 0x314 */
    u16 m_ENC_REG_INPUT_CONTROL;            /* 0x316 */
    u16 m_ENC_REG_RATE_CONTROL;             /* 0x318 */
    u16 m_ENC_REG_BIT_RATE;                 /* 0x31A */
    u16 m_ENC_REG_FILTER_CONTROL;           /* 0x31C */
    u16 m_ENC_REG_GOP_LOOP_FILTER;          /* 0x31E */
    u16 m_ENC_REG_ET_CONTROL;               /* 0x320 */
    u16 m_ENC_REG_BLOCK_SIZE;               /* 0x322 */
    u16 m_ENC_REG_OUT_PIC_RESOLUTION;       /* 0x324 */
    u16 m_ENC_REG_AUDIO_CONTROL_PARAM;      /* 0x326 */
    u16 m_ENC_REG_AUDIO_CONTROL_EX;         /* 0x328 */
    u8 _pad2[6];                            /* 0x32A */

    struct c_object_mgr *m_pChannelMgr;

    /* Video output config */
    int m_VOEnable;                         /* 0x338 */
    u32 m_VOUMode;                          /* 0x33C */
    u32 m_VOUStartPixel;                    /* 0x340 */
    u32 m_VOUStartLine;                     /* 0x344 */

    /* Audio output config */
    int m_AOEnable;                         /* 0x348 */
    u32 m_AOControls_ao_msb;                /* 0x34C */
    u32 m_AOControls_lrclk_i;               /* 0x350 */
    u32 m_AOControls_bclk_i;                /* 0x354 */
    u32 m_AOControls_ao_i2s;                /* 0x358 */
    u32 m_AOControls_ao_rj;                 /* 0x35C */
    u32 m_AOControls_ao_s;                  /* 0x360 */
    u32 m_AOControls;                       /* 0x364 */
    u32 m_AudControls;                      /* 0x368 */

    /* Video input config */
    u32 m_VIUMode;                          /* 0x36C */
    u32 m_VIUFormat;                        /* 0x370 */
    u32 m_VIUStartPixel;                    /* 0x374 */
    u32 m_VIUStartLine;                     /* 0x378 */
    u32 m_ClkEdge;                          /* 0x37C */
    u32 m_ulViuSyncCode1;                   /* 0x380 */
    u32 m_ulViuSyncCode2;                   /* 0x384 */

    /* Audio input config */
    u32 m_AIControls_ai_msb;                /* 0x388 */
    u32 m_AIControls_lrclk_i;               /* 0x38C */
    u32 m_AIControls_bclk_i;                /* 0x390 */
    u32 m_AIControls_ai_i2s;                /* 0x394 */
    u32 m_AIControls_ai_rj;                 /* 0x398 */
    u32 m_AIControls_ai_m;                  /* 0x39C */

    /* Firmware versions */
    u32 m_ulEncFirmwareVer;                 /* 0x3A0 */
    u32 m_ulDecFirmwareVer;                 /* 0x3A4 */
    u32 m_ulSysFirmwareVer;                 /* 0x3A8 */
    u32 m_ulAudFirmwareVer;                 /* 0x3AC */

    /* Interrupt state */
    int m_bInterruptAttached;               /* 0x3B0 */
    u32 m_interruptNumber;                  /* 0x3B4 */
    void *m_isrID;                          /* 0x3B8 */

    /* Memory/chip info */
    u32 m_MemType;                          /* 0x3C0 */
    u32 m_MemSize;                          /* 0x3C4 */
    u8 m_ChipType;                          /* 0x3C8 */
    u8 _pad3[3];                            /* 0x3C9 */
    u32 m_ChipVersion;                      /* 0x3CC */
    u32 m_VerFwAPI;                         /* 0x3D0 */
    u32 m_FwIntMode;                        /* 0x3D4 */
    u32 m_FwFixedMode;                      /* 0x3D8 */

    /* GPIO state */
    u32 m_GPIODirections;                   /* 0x3DC */
    u32 m_GPIOValues;                       /* 0x3E0 */
    u8 _pad4[4];                            /* 0x3E4 */

    /* Synchronization */
    void *m_SemaphoreFWAPI;                 /* 0x3E8 */
    void *m_EvtTask;                        /* 0x3F0 */
    void *m_EvtTaskReply;                   /* 0x3F8 */
    void *m_EvtSyncDma;                     /* 0x400 */

    /* Power/error state */
    u32 m_State;                            /* 0x408 */
    u32 m_PowerState;                       /* 0x40C */
    int m_ErrorRecovery;                    /* 0x410 */
    u32 m_USBDefaultMode;                   /* 0x414 */
    u32 m_Pll4;                             /* 0x418 */
    u32 m_Pll5;                             /* 0x41C */
    u32 m_UseArtesaExt340;                  /* 0x420 */
    u32 m_AIVolume;                         /* 0x424 */
    u32 m_AOVolume;                         /* 0x428 */
};

/* ============================================
 * PCI Memory Range (0x18 = 24 bytes)
 * ============================================ */
struct pci_memory_range {
    u32 Length;                             /* 0x00 */
    u8 _pad[4];                             /* 0x04 */
    u64 PhysicalAddress;                    /* 0x08 */
    void *VirtualAddress;                   /* 0x10 */
};

/* ============================================
 * Device Transfer (0x48 = 72 bytes)
 * ============================================ */
struct device_transfer {
    void *DmaAdapter;                       /* 0x00 */
    enum dma_dir DmaDirection;              /* 0x08 */
    u32 DmaChannel;                         /* 0x0C */
    u32 DmaIndex;                           /* 0x10 */
    u8 _pad1[4];                            /* 0x14 */
    struct {
        void *VirtAddress;
        u64 PhysAddress;
    } DmaDescriptor;                        /* 0x18 */
    u32 MapRegsAvailable;                   /* 0x28 */
    u32 NumDescriptors;                     /* 0x2C */
    u32 UsedDescriptors;                    /* 0x30 */
    u32 Timeout;                            /* 0x34 */
    void *hDmaRequest;                      /* 0x38 */
    u8 bStarted;                            /* 0x40 */
    u8 _pad2[7];                            /* 0x41 */
};

/* ============================================
 * PCI DMA Buffer (0x70 = 112 bytes)
 * ============================================ */
struct pci_dma_buffer {
    u32 Signature;                          /* 0x00 */
    u8 _pad1[4];                            /* 0x04 */
    struct list_head ListEntry;             /* 0x08 */
    struct pcie_device_extension *pDeviceExtension; /* 0x18 */
    void *pDmaAdapter;                      /* 0x20 */
    void *pMdlAddressCaller;                /* 0x28 */
    void *pMdl;                             /* 0x30 */
    void *pScatterGatherList;               /* 0x38 */
    u8 IsWrite;                             /* 0x40 */
    u8 _pad2[7];                            /* 0x41 */
    void *pGetSgListCB;                     /* 0x48 */
    void *pGetSgListUD;                     /* 0x50 */
    struct list_head DmaRequestList;        /* 0x58 */
    spinlock_t DmaRequestListLock;          /* 0x68 */
};

/* ============================================
 * PCI DMA Request (0x30 = 48 bytes)
 * ============================================ */
struct pci_dma_request {
    struct list_head ListEntry;             /* 0x00 */
    struct pci_dma_buffer *pDmaBuffer;      /* 0x10 */
    u32 NumBytes;                           /* 0x18 */
    u32 NumDescriptors;                     /* 0x1C */
    void *pPerformanceMdl;                  /* 0x20 */
    void *pRequestCB;                       /* 0x28 */
};

/* ============================================
 * PCIe Device Extension (0x1788 bytes)
 * ============================================ */
struct pcie_device_extension {
    void *pPCIeCntl;                        /* 0x00 */
    void *FunctionalDeviceObject;           /* 0x08 */
    void *PhysicalDeviceObject;             /* 0x10 */
    void *LowerDeviceObject;                /* 0x18 */
    void *InterruptObject;                  /* 0x20 */
    u8 InterfaceType;                       /* 0x28 */
    u8 _pad1[3];                            /* 0x29 */
    u32 BusType;                            /* 0x2C */
    u32 BusNumber;                          /* 0x30 */
    u8 InterruptLevel;                      /* 0x34 */
    u8 _pad2[3];                            /* 0x35 */
    u32 InterruptVector;                    /* 0x38 */
    u8 _pad3[4];                            /* 0x3C */
    u64 InterruptAffinity;                  /* 0x40 */
    u32 InterruptMode;                      /* 0x48 */
    u8 TimerStarted;                        /* 0x4C */
    u8 _pad4[3];                            /* 0x4D */
    void *pTimerDpc;                        /* 0x50 */
    void *pTimer;                           /* 0x58 */
    u8 StartEvent[0x18];                    /* 0x60 */
    u8 StopEvent[0x18];                     /* 0x78 */
    u8 RemoveEvent[0x18];                   /* 0x90 */
    u32 State;                              /* 0xA8 */
    u32 OutstandingIO;                      /* 0xAC */
    u32 InterruptStatus;                    /* 0xB0 */
    u32 InterruptEnable;                    /* 0xB4 */
    u8 Dpc[0x40];                           /* 0xB8 */
    struct device_transfer DmaDevices[64];  /* 0xF8 - 64 * 0x48 = 0x1200 */
    int ReadDmaChannel;                     /* 0x12F8 */
    int WriteDmaChannel;                    /* 0x12FC */
    u8 DmaBufferList[0x10];                 /* 0x1300 */
    u64 DmaBufferListLock;                  /* 0x1310 */
    u64 DmaQueueLock;                       /* 0x1318 */
    u8 DmaQueueLockIrql;                    /* 0x1320 */
    u8 _pad5[1];                            /* 0x1321 */
    u16 m_dmaBar;                           /* 0x1322 */
    u16 m_numBars;                          /* 0x1324 */
    u16 m_CardConfigId;                     /* 0x1326 */
    u16 m_NumDmaAvailable;                  /* 0x1328 */
    u16 m_InterruptAvailable;               /* 0x132A */
    u8 m_64BitAddress;                      /* 0x132C */
    u8 m_64BitCapable;                      /* 0x132D */
    u8 _pad6[2];                            /* 0x132E */
    u32 m_DmaMaxTransfer;                   /* 0x1330 */
    u8 _pad7[4];                            /* 0x1334 */
    struct pci_memory_range m_MemoryRange[6]; /* 0x1338 */
    struct pci_memory_range m_IoRange[6];   /* 0x13C8 */
    u8 m_pciConfig[0x100];                  /* 0x1458 */
    u8 m_pciHeader[0x188];                  /* 0x1558 */
    struct pci_rw_ram_struct m_ReadInfo;    /* 0x16E0 */
    struct pci_rw_ram_struct m_WriteInfo;   /* 0x1719 */
    u8 _pad8[6];                            /* 0x1752 */
    void __iomem *pRegisters;               /* 0x1758 */
    void __iomem *pRegistersEx;             /* 0x1760 */
    u64 DmaStartTime;                       /* 0x1768 */
    u64 IrpStartTime;                       /* 0x1770 */
    void *pBusCallbackFunc;                 /* 0x1778 */
    void *pBusCallbackContext;              /* 0x1780 */
};

/* PED_DMA_DESCRIPTOR (0x20 = 32 bytes) */
struct ped_dma_descriptor {
    u32 Control;                /* 0x00 */
    u32 ByteCount;              /* 0x04 */
    u64 SystemAddress;          /* 0x08 */
    u64 CardAddress;            /* 0x10 */
    u64 NextDescriptor;         /* 0x18 */
};

/* PED_DMA_ENGINE (0x100 = 256 bytes) */
struct ped_dma_engine {
    u32 Capabilities;           /* 0x00 */
    u32 ControlStatus;          /* 0x04 */
    u64 Descriptor;             /* 0x08 - _LARGE_INTEGER in Windows */
    u32 HardwareTime;           /* 0x10 */
    u32 ChainCmplByteCount;     /* 0x14 */
    u32 Reserved[58];           /* 0x18 */
};

/* ============================================
 * ICodecLib (0x68 = 104 bytes)
 * ============================================ */
struct i_codec_lib {
    void *InitDevice;                       /* 0x00 */
    void *Release;                          /* 0x08 */
    void *Reset;                            /* 0x10 */
    void *Set;                              /* 0x18 */
    void *Get;                              /* 0x20 */
    void *GetMpegCodec;                     /* 0x28 */
    void *GetVideoDecoder;                  /* 0x30 */
    void *GetVideoEncoder;                  /* 0x38 */
    void *GetAudioCodec;                    /* 0x40 */
    void *GetTuner;                         /* 0x48 */
    void *GetTVAudio;                       /* 0x50 */
    void *Disable;                          /* 0x58 */
    void *Enable;                           /* 0x60 */
};

/* ============================================
 * QP MpegCodec InitData (0xE0 = 224 bytes)
 * ============================================ */
struct qp_mpgcodec_initdata {
    int bDontInitHW;                        /* 0x00 */
    u32 AccessMode;                         /* 0x04 */
    u32 BusType;                            /* 0x08 */
    u32 MemType;                            /* 0x0C */
    u32 MemSize;                            /* 0x10 */
    u8 ChipType;                            /* 0x14 */
    u8 _pad1[3];                            /* 0x15 */
    void *RegisterBase;                     /* 0x18 */
    void *MemoryBase;                       /* 0x20 */
    u32 PageSize;                           /* 0x28 */
    u32 InterruptNumber;                    /* 0x2C */
    u32 VIUMode;                            /* 0x30 */
    u32 VIUFormat;                          /* 0x34 */
    u32 VIUStartPixel;                      /* 0x38 */
    u32 VIUStartLine;                       /* 0x3C */
    u32 ClkEdge;                            /* 0x40 */
    u32 VIUSyncCode1;                       /* 0x44 */
    u32 VIUSyncCode2;                       /* 0x48 */
    u32 AIControls_ai_msb;                  /* 0x4C */
    u32 AIControls_lrclk_i;                 /* 0x50 */
    u32 AIControls_bclk_i;                  /* 0x54 */
    u32 AIControls_ai_i2s;                  /* 0x58 */
    u32 AIControls_ai_rj;                   /* 0x5C */
    u32 AIControls_ai_m;                    /* 0x60 */
    int VOEnable;                           /* 0x64 */
    u32 VOUMode;                            /* 0x68 */
    u32 VOUStartPixel;                      /* 0x6C */
    u32 VOUStartLine;                       /* 0x70 */
    int AOEnable;                           /* 0x74 */
    u32 AOControls_ao_msb;                  /* 0x78 */
    u32 AOControls_lrclk_i;                 /* 0x7C */
    u32 AOControls_bclk_i;                  /* 0x80 */
    u32 AOControls_ao_i2s;                  /* 0x84 */
    u32 AOControls_ao_rj;                   /* 0x88 */
    u32 AOControls_ao_s;                    /* 0x8C */
    void *BusData;                          /* 0x90 */
    u32 BusDataSize;                        /* 0x98 */
    u32 VerFwAPI;                           /* 0x9C */
    u32 FwIntMode;                          /* 0xA0 */
    u32 FwFixedMode;                        /* 0xA4 */
    u32 GPIODirections;                     /* 0xA8 */
    u32 GPIOValues;                         /* 0xAC */
    u32 ErrorRecovery;                      /* 0xB0 */
    int bUseExtFW;                          /* 0xB4 */
    u8 *pVideoFW;                           /* 0xB8 */
    u8 *pAudioFW;                           /* 0xC0 */
    u32 VidFWSize;                          /* 0xC8 */
    u32 AudFWSize;                          /* 0xCC */
    int bRestoreUSBMode;                    /* 0xD0 */
    u32 Pll4;                               /* 0xD4 */
    u32 Pll5;                               /* 0xD8 */
    u32 UseArtesaExt340;                    /* 0xDC */
};


struct qp_i2c_initdata {
    u32 type;
    u32 clk;
    u32 data;
};

struct qp_vbi_dataformat {
    int Top_StartLine;          /* 0x00 */
    int Top_EndLine;            /* 0x04 */
    u32 Top_StartPixel;         /* 0x08 */
    int Top_SamplesPerLine;     /* 0x0C */
    int Bottom_StartLine;       /* 0x10 */
    int Bottom_EndLine;         /* 0x14 */
    u32 Bottom_StartPixel;      /* 0x18 */
    int Bottom_SamplesPerLine;  /* 0x1C */
    int BufferSize;             /* 0x20 */
    u32 nFrameRate;             /* 0x24 */
};                              /* Size: 0x28 */
/* ============================================
 * QP CodecLib InitData (0x120 = 288 bytes)
 * ============================================ */
struct qp_codeclib_initdata {
    int bDownloadFW;                        /* 0x00 */
    u8 _pad1[4];                            /* 0x04 */
    struct qp_mpgcodec_initdata mpgCodecInitData; /* 0x08 */
    u8 vidDecoderInitData[12];              /* 0xE8 */
    u8 vidEncoderInitData[8];               /* 0xF4 */
    u8 tunerInitData[4];                    /* 0xFC */
    u8 tvAudioInitData[4];                  /* 0x100 */
    u8 audCodecInitData[4];                 /* 0x104 */
    struct qp_i2c_initdata i2cInitData;      /* 0x108 */
    struct qp_i2c_initdata i2cInitDataEx;    /* 0x114 */

};

/* ============================================
 * CQLCodecLib (0x240 = 576 bytes)
 * ============================================ */
struct cql_codec_lib {
    struct c_object m_Object;               /* 0x000 */
    struct i_codec_lib m_iCodecLib;         /* 0x038 */
    void *m_pDO;                            /* 0x0A0 */
    void *m_PDOLayered;                     /* 0x0A8 */
    struct qp_codeclib_initdata m_InitData; /* 0x0B0 */
    void *m_pDeviceCallback;                /* 0x1D0 */
    void *m_callbackContext;                /* 0x1D8 */
    void *m_pUsbInterface;                  /* 0x1E0 */
    void *m_pPCIeCntl;                      /* 0x1E8 */
    void *m_pUsbCntl;                       /* 0x1F0 */
    struct cql_codec *m_pMpgCodec;          /* 0x1F8 */
    void *m_pI2CProvider;                   /* 0x200 */
    void *m_pI2CProviderEx;                 /* 0x208 */
    void *m_pVidDecoder;                    /* 0x210 */
    void *m_pVidEncoder;                    /* 0x218 */
    void *m_pAudCodec;                      /* 0x220 */
    void *m_pTuner;                         /* 0x228 */
    void *m_pTVAudio;                       /* 0x230 */
    u32 m_dwDeviceError;                    /* 0x238 */
    u8 _pad1[4];                            /* 0x23C */
};

/* ============================================
 * Project_C985 (0xAC = 172 bytes)
 * ============================================ */
struct project_c985 {
    u8 _padding[8];                         /* 0x00 */
    void *m_pDevice;                        /* 0x08 */
    struct i_codec_lib *m_pCodec;           /* 0x10 */
    struct i_mpeg_codec *m_pMpegCodec;      /* 0x18 */
    void *m_pNuc100;                        /* 0x20 - InterfaceNUC100* */
    void *m_pTi3101;                        /* 0x28 - TI3101* */
    enum c985_video_input m_c985_video_in;  /* 0x30 */
    enum c985_audio_input m_c985_audio_in;  /* 0x34 */
    u8 m_gpio_mcu_reset;                    /* 0x38 */
    u8 m_gpio_video_select;                 /* 0x39 */
    u8 m_gpio_audio_select_1;               /* 0x3A */
    u8 m_gpio_audio_select_2;               /* 0x3B */
    int m_hdmi_match_case;                  /* 0x3C */
    int m_hdmi_status;                      /* 0x40 */
    int m_hdmi_hdcp_status;                 /* 0x44 */
    struct hdmi_info m_hdmi_video_info;     /* 0x48 */
    u32 m_hdmi_audio_freq;                  /* 0x68 */
    int m_hdmi_with_audio;                  /* 0x6C */
    void *m_pthread;                        /* 0x70 - COSDependThread* */
    struct hw_config m_averHwConfig;        /* 0x78 */
    int m_bisEDIDmerge;                     /* 0x9C */
    int m_bisVersionD;                      /* 0xA0 */
    int m_bisMonitorThreadPause;            /* 0xA4 */
    u32 m_dwVolume_ext_audio;               /* 0xA8 */
};

/* ============================================
 * CYUVInChannel (extends CChannel)
 * Total: 0x11A8 (4520 bytes)
 * ============================================ */
struct c_yuv_in_channel {
    struct c_channel m_Channel;             /* 0x0000 - 4496 bytes (0x1190) */
    u32 m_dwFrameSize;                      /* 0x1190 - 4 bytes */
    u32 m_ulVideoFramesCount;               /* 0x1194 - 4 bytes */
    int m_lWidth;                           /* 0x1198 - 4 bytes */
    int m_lHeight;                          /* 0x119C - 4 bytes */
    int m_nBitCount;                        /* 0x11A0 - 4 bytes */
    int m_nDataType;                        /* 0x11A4 - 4 bytes */
};                                          /* Total: 4520 bytes (0x11A8) */

/* ============================================
 * V4L2 Buffer Wrapper (Linux-specific)
 * ============================================ */
struct c985_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
    struct qp_buffer_descriptor *buf_desc;
    struct qp_ksstream_header *header;
    struct queue_entry *queue_entry;
};

/* ============================================
 * c985_poc - Main Device Structure
 * ============================================ */
struct c985_poc {
    u32 active_task_id;  /* Currently active encoder task (0-7, or -1 if none) */

    /* Project structure - C985-specific logic */
    struct project_c985 project;

    /* Windows driver structures */
    struct pcie_device_extension pcie;
    struct cql_codec codec;

    /* Linux PCI */
    struct pci_dev *pdev;
    int irq_registered;

    /* Linux V4L2 */
    struct v4l2_device v4l2_dev;
    bool v4l2_registered;
    struct video_device vdev;
    struct vb2_queue vb2_queue;
    struct mutex v4l2_lock;
    struct list_head buf_list;
    spinlock_t buf_lock;
    unsigned int width;
    unsigned int height;
    unsigned int sequence;
    int hdmi_valid;
    int hdmi_info_cached;
    struct hdmi_info cached_hdmi_info;

    /* Frame handling */
    DECLARE_KFIFO_PTR(frame_fifo, u8);
    spinlock_t frame_lock;
    struct list_head pending_frames;
    struct work_struct frame_work;
    u32 frame_sequence;
    bool encoder_running;

    /* IRQ/Work */
    spinlock_t irq_lock;
    struct work_struct irq_work;
    struct work_struct dma_work;

    /* Completion/DMA tracking */
    struct completion mailbox_complete;
    struct completion dma_done;
    u32 dma_interrupt_status;
    int dma_engine_idx[64];
    spinlock_t dma_lock;
    bool dma_active;
    struct pci_dma_request *dma_requests[64];

    /* Firmware state */
    u32 qpsos_version;
    u32 config_base;

    /* Debug */
    struct dentry *debug_dir;

    /* Hardware config (C985-specific) */
    u8 m_McuAddr;
    u8 m_McuRstGpio;
    u8 m_AlgAudAddr;
    u8 m_AlgAudRstGpio;
    u8 m_AudSwitchGpio1;
    u8 m_AudSwitchGpio2;

    u32 m_I2cType;
    u32 m_I2cGpioClk;
    u32 m_I2cGpioData;
    u32 m_I2cExType;

    u32 m_VidInputType;
    u32 m_VidInputChannel;

    /* Encoder configuration */
    u32 m_EncFunction;
    u32 m_EncSystemControl;
    u32 m_EncRateControl;
    u32 m_EncRateControlEx;
    u32 m_EncGopLoopFilter;
    u32 m_EncPictureResolution;
    u32 m_EncOutPicResolution;
    u32 m_EncInputControl;
    u32 m_EncSyncMode;
    u32 m_EncBitRate;
    u32 m_EncFilterControl;
    u32 m_EncEtControl;
    u32 m_EncBlockSize;
    u32 m_EncStopMode;
    u32 m_EncEnableVidPadding;
    u32 m_EncLinkVin;
    u32 m_EncLinkVout;
    u32 m_EncLinkAin;
    u32 m_EncLinkAout;
    u32 m_EncAudioControlParam;
    u32 m_EncAudioControlExAac;
    u32 m_EncAudioControlExG711;
    u32 m_EncAudioControlExLpcm;
    u32 m_EncAudioControlExSilk;
    u32 m_EncLargeCompressBufCtrl;
    u32 m_EncMjpegQuality;
    u32 m_EncMjpegFrameBuffer;
    u32 m_EncIndexCapFreq;
    u32 m_EncMp4VideoBlockNumber;
    u32 m_EncDecimationInputFormat;
    u32 m_EncDecimationOutputFormat;
    u32 m_EncDecimationScaleFactor;
    u32 m_EncDeinterlaceMode;

    /* Event/synchronization */
    wait_queue_head_t evt_task;
    u32 evt_task_flags;
    wait_queue_head_t evt_task_reply;
    u32 evt_task_reply_flags;
    wait_queue_head_t evt_sync_dma;
    u32 evt_sync_dma_flags;
    struct mutex mailbox_lock;
};

/* ============================================
 * Accessor Macros
 * ============================================ */
#define c985_bar0(d)    ((d)->pcie.pRegisters)
#define c985_bar1(d)    ((d)->pcie.pRegistersEx)
#define c985_hci(d)     (&(d)->codec.m_hci)
#define c985_task(d)    ((d)->codec.m_pTask)


#endif /* C985_STRUCTS_H */
