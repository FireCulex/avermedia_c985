/* SPDX-License-Identifier: GPL-2.0 */
#ifndef STRUCTS_H
#define STRUCTS_H

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>

/* ============================================
 * Error Codes (_EQPErrors)
 * ============================================ */
#define QPERR_SUCCESS       0
#define QPERR_PENDING       1
#define QPERR_CTRL_VERSION  245
#define QPERR_AGAIN         246
#define QPERR_NOTFOUND      247
#define QPERR_CANCELLED     248
#define QPERR_INVALID       249
#define QPERR_TIMEOUT       250
#define QPERR_NOTSUPP       251
#define QPERR_NOTIMPL       252
#define QPERR_PARMS         253
#define QPERR_NOMEM         254
#define QPERR_FAIL          255

/* ============================================
 * Enums
 * ============================================ */
enum qphci_mode {
    QPHCI_MODE_DIRECT = 0,
    QPHCI_MODE_INDIRECT = 1,
};

enum qphci_bus {
    QPHCI_BUS_USB = 0,
    QPHCI_BUS_PCI = 1,
    QPHCI_BUS_EMU = 2,
};

enum dma_dir {
    DMA_DIR_READ = 0,
    DMA_DIR_WRITE = 1,
};

enum channel_direction {
    CHANNEL_DIR_IN = 0,
    CHANNEL_DIR_OUT = 1,
};

/* ============================================
 * Forward Declarations
 * ============================================ */
struct c985_poc;
struct cql_codec;
struct ihciapi;
struct c_task;

/* ============================================
 * CObject (0x38 bytes)
 * ============================================ */
struct c_object {
    /* 0x00 */ void *Init;
    /* 0x08 */ void *Done;
    /* 0x10 */ struct c_object *m_pParent;
    /* 0x18 */ int m_fInitialized;
    /* 0x1C */ u32 m_dwObjectAttributes;
    /* 0x20 */ void *m_semCriticalSection;
    /* 0x28 */ u64 m_spinlock;
    /* 0x30 */ u8 m_irql;
    /* 0x31 */ u8 _pad[7];
};

/* ============================================
 * CThread (0x60 bytes)
 * ============================================ */
struct c_thread {
    /* 0x00 */ void *m_ThreadProc;
    /* 0x08 */ void *ThreadExit;
    /* 0x10 */ void *m_threadID;
    /* 0x18 */ void *m_context;
    /* 0x20 */ u32 m_priority;
    /* 0x24 */ u8 _pad1[4];
    /* 0x28 */ void *m_EvtWait;
    /* 0x30 */ void *m_EvtReply;
    /* 0x38 */ int m_bRemoveUserSpaceMapping;
    /* 0x3C */ char m_szThreadName[32];
    /* 0x5C */ u8 _pad2[4];
};

/* ============================================
 * IMpegCodec (0xC8 bytes)
 * ============================================ */
struct i_mpeg_codec {
    /* 0x00 */ void *InitDevice;
    /* 0x08 */ void *Release;
    /* 0x10 */ void *Reset;
    /* 0x18 */ void *Set;
    /* 0x20 */ void *Get;
    /* 0x28 */ void *AllocEncodeTask;
    /* 0x30 */ void *AllocDecodeTask;
    /* 0x38 */ void *ReleaseTask;
    /* 0x40 */ void *Open;
    /* 0x48 */ void *Close;
    /* 0x50 */ void *Start;
    /* 0x58 */ void *Stop;
    /* 0x60 */ void *Acquire;
    /* 0x68 */ void *Pause;
    /* 0x70 */ void *Step;
    /* 0x78 */ void *SetRate;
    /* 0x80 */ void *GetRate;
    /* 0x88 */ void *BeginFlush;
    /* 0x90 */ void *Flush;
    /* 0x98 */ void *EndFlush;
    /* 0xA0 */ void *AddBuffer;
    /* 0xA8 */ void *CancelBuffer;
    /* 0xB0 */ void *TimeoutBuffer;
    /* 0xB8 */ void *GetTime;
    /* 0xC0 */ void *XferData;
};

/* ============================================
 * IHCIAPI (0x1C0 bytes)
 * ============================================ */
struct ihciapi {
    /* 0x00 */  struct c_object m_Object;
    /* 0x38 */  struct c_thread m_Thread;

    /* 0x98 */  void *ReadHciRegister;
    /* 0xA0 */  void *WriteHciRegister;
    /* 0xA8 */  void *RegisterRead;
    /* 0xB0 */  void *RegisterWrite;
    /* 0xB8 */  void *RegisterReadEx;
    /* 0xC0 */  void *RegisterWriteEx;
    /* 0xC8 */  void *MemoryRead;
    /* 0xD0 */  void *MemoryReadEx;
    /* 0xD8 */  void *MemoryWrite;
    /* 0xE0 */  void *StartDMAWrite;
    /* 0xE8 */  void *StartDMARead;
    /* 0xF0 */  void *ResetArm;
    /* 0xF8 */  void *SetInterrupt;
    /* 0x100 */ void *ClearInterrupt;
    /* 0x108 */ void *EnableInterrupts;
    /* 0x110 */ void *DisableInterrupts;
    /* 0x118 */ void *GetInterruptsStatus;
    /* 0x120 */ void *GetInterruptMask;
    /* 0x128 */ void *SetInterruptMask;
    /* 0x130 */ void *CopyFromCommonBuffer;
    /* 0x138 */ void *DMAReadDone;
    /* 0x140 */ void *DMAWriteDone;
    /* 0x148 */ void *DMAXferDone;

    /* 0x150 */ struct cql_codec *m_pMpegCodec;
    /* 0x158 */ enum qphci_mode m_access_mode;
    /* 0x15C */ enum qphci_bus m_bus_type;
    /* 0x160 */ u8 *m_pRegisterBase;
    /* 0x168 */ u8 *m_pMemoryBase;
    /* 0x170 */ u32 m_PageSize;
    /* 0x174 */ u32 m_mem_mapping_start_addr[3];
    /* 0x180 */ u32 m_mem_mapping_end_addr[3];
    /* 0x18C */ u32 m_mem_mapping_offset[3];
    /* 0x198 */ void *m_EvtEmuTask;
    /* 0x1A0 */ void *m_EvtEmuTaskReply;
    /* 0x1A8 */ void *m_pUsbCntl;
    /* 0x1B0 */ void *m_pPCIeCntl;
    /* 0x1B8 */ int m_bEmulationMode;
    /* 0x1BC */ u32 m_ulChipVer;
};

/* ============================================
 * CQLCodec (0x42C bytes)
 * ============================================ */
struct cql_codec {
    /* 0x000 */ struct c_object m_Object;
    /* 0x038 */ struct i_mpeg_codec m_iMpegCodec;
    /* 0x100 */ struct ihciapi m_hci;

    /* 0x2C0 */ int m_bHCIInited;
    /* 0x2C4 */ u8 _pad1[4];
    /* 0x2C8 */ void *m_pUsbCntl;
    /* 0x2D0 */ void *m_pPCIeCntl;
    /* 0x2D8 */ void *m_pDeviceCallback;
    /* 0x2E0 */ void *m_callbackContext;
    /* 0x2E8 */ struct c_task *m_pTask;
    /* 0x2F0 */ u8 *m_pVideoFW;
    /* 0x2F8 */ u8 *m_pAudioFW;
    /* 0x300 */ u32 m_QL201FWSize;
    /* 0x304 */ u32 m_QL201AudFWSize;
    /* 0x308 */ int m_bVideoFWUpdated;
    /* 0x30C */ int m_bAudioFWUpdated;

    /* 0x310 */ u16 m_ENC_REG_MESSAGE;
    /* 0x312 */ u16 m_ENC_REG_SYSTEM_CONTROL;
    /* 0x314 */ u16 m_ENC_REG_PICTURE_RESOLUTION;
    /* 0x316 */ u16 m_ENC_REG_INPUT_CONTROL;
    /* 0x318 */ u16 m_ENC_REG_RATE_CONTROL;
    /* 0x31A */ u16 m_ENC_REG_BIT_RATE;
    /* 0x31C */ u16 m_ENC_REG_FILTER_CONTROL;
    /* 0x31E */ u16 m_ENC_REG_GOP_LOOP_FILTER;
    /* 0x320 */ u16 m_ENC_REG_ET_CONTROL;
    /* 0x322 */ u16 m_ENC_REG_BLOCK_SIZE;
    /* 0x324 */ u16 m_ENC_REG_OUT_PIC_RESOLUTION;
    /* 0x326 */ u16 m_ENC_REG_AUDIO_CONTROL_PARAM;
    /* 0x328 */ u16 m_ENC_REG_AUDIO_CONTROL_EX;
    /* 0x32A */ u8 _pad2[6];

    /* 0x330 */ void *m_pChannelMgr;

    /* 0x338 */ int m_VOEnable;
    /* 0x33C */ u32 m_VOUMode;
    /* 0x340 */ u32 m_VOUStartPixel;
    /* 0x344 */ u32 m_VOUStartLine;

    /* 0x348 */ int m_AOEnable;
    /* 0x34C */ u32 m_AOControls_ao_msb;
    /* 0x350 */ u32 m_AOControls_lrclk_i;
    /* 0x354 */ u32 m_AOControls_bclk_i;
    /* 0x358 */ u32 m_AOControls_ao_i2s;
    /* 0x35C */ u32 m_AOControls_ao_rj;
    /* 0x360 */ u32 m_AOControls_ao_s;
    /* 0x364 */ u32 m_AOControls;
    /* 0x368 */ u32 m_AudControls;

    /* 0x36C */ u32 m_VIUMode;
    /* 0x370 */ u32 m_VIUFormat;
    /* 0x374 */ u32 m_VIUStartPixel;
    /* 0x378 */ u32 m_VIUStartLine;
    /* 0x37C */ u32 m_ClkEdge;
    /* 0x380 */ u32 m_ulViuSyncCode1;
    /* 0x384 */ u32 m_ulViuSyncCode2;

    /* 0x388 */ u32 m_AIControls_ai_msb;
    /* 0x38C */ u32 m_AIControls_lrclk_i;
    /* 0x390 */ u32 m_AIControls_bclk_i;
    /* 0x394 */ u32 m_AIControls_ai_i2s;
    /* 0x398 */ u32 m_AIControls_ai_rj;
    /* 0x39C */ u32 m_AIControls_ai_m;

    /* 0x3A0 */ u32 m_ulEncFirmwareVer;
    /* 0x3A4 */ u32 m_ulDecFirmwareVer;
    /* 0x3A8 */ u32 m_ulSysFirmwareVer;
    /* 0x3AC */ u32 m_ulAudFirmwareVer;

    /* 0x3B0 */ int m_bInterruptAttached;
    /* 0x3B4 */ u32 m_interruptNumber;
    /* 0x3B8 */ void *m_isrID;

    /* 0x3C0 */ u32 m_MemType;
    /* 0x3C4 */ u32 m_MemSize;
    /* 0x3C8 */ u8 m_ChipType;
    /* 0x3C9 */ u8 _pad3[3];
    /* 0x3CC */ u32 m_ChipVersion;
    /* 0x3D0 */ u32 m_VerFwAPI;
    /* 0x3D4 */ u32 m_FwIntMode;
    /* 0x3D8 */ u32 m_FwFixedMode;

    /* 0x3DC */ u32 m_GPIODirections;
    /* 0x3E0 */ u32 m_GPIOValues;
    /* 0x3E4 */ u8 _pad4[4];

    /* 0x3E8 */ void *m_SemaphoreFWAPI;
    /* 0x3F0 */ void *m_EvtTask;
    /* 0x3F8 */ void *m_EvtTaskReply;
    /* 0x400 */ void *m_EvtSyncDma;

    /* 0x408 */ u32 m_State;
    /* 0x40C */ u32 m_PowerState;
    /* 0x410 */ int m_ErrorRecovery;
    /* 0x414 */ u32 m_USBDefaultMode;
    /* 0x418 */ u32 m_Pll4;
    /* 0x41C */ u32 m_Pll5;
    /* 0x420 */ u32 m_UseArtesaExt340;
    /* 0x424 */ u32 m_AIVolume;
    /* 0x428 */ u32 m_AOVolume;
};

/* ============================================
 * DEVICE_TRANSFER (0x48 bytes)
 * ============================================ */
struct device_transfer {
    /* 0x00 */ void *DmaAdapter;
    /* 0x08 */ enum dma_dir DmaDirection;
    /* 0x0C */ u32 DmaChannel;
    /* 0x10 */ u32 DmaIndex;
    /* 0x14 */ u8 _pad1[4];
    /* 0x18 */ struct {
        void *VirtAddress;
        u64 PhysAddress;
    } DmaDescriptor;
    /* 0x28 */ u32 MapRegsAvailable;
    /* 0x2C */ u32 NumDescriptors;
    /* 0x30 */ u32 UsedDescriptors;
    /* 0x34 */ u32 Timeout;
    /* 0x38 */ void *hDmaRequest;
    /* 0x40 */ u8 bStarted;
    /* 0x41 */ u8 _pad2[7];
};

/* ============================================
 * _TASK_DMA_REQUEST (0x1C bytes, pack(1))
 * ============================================ */
struct task_dma_request {
    /* 0x00 */ u32 ulArmAddr;
    /* 0x04 */ u8 *pBuffer;
    /* 0x0C */ u32 ulLength;
    /* 0x10 */ u32 ulOffset;
    /* 0x14 */ enum channel_direction dir;
    /* 0x18 */ int bSwap;
} __packed;

/* ============================================
 * _TASK_IO_PENDING (0x2C bytes)
 * ============================================ */
struct task_io_pending {
    /* 0x00 */ u32 ulArmAddress;
    /* 0x04 */ u8 *pHostAddress;
    /* 0x0C */ u32 ulLength;
    /* 0x10 */ u32 ulXferMode;
    /* 0x14 */ u32 ulPicWidth;
    /* 0x18 */ u32 ulPicHeight;
    /* 0x1C */ u8 status;
    /* 0x1D */ u8 _pad1[3];
    /* 0x20 */ void *pTaskData;
    /* 0x28 */ u32 dataType;
};

/* ============================================
 * _TASK_DATA (0x64C bytes)
 * ============================================ */
struct task_data {
    /* 0x00 */  struct c_object m_Object;
    /* 0x38 */  u32 id;
    /* 0x3C */  int valid;
    /* 0x40 */  u32 type;
    /* 0x44 */  u32 m_dwSession;
    /* 0x48 */  u32 m_dwStarted;
    /* 0x4C */  u32 m_dwPaused;
    /* 0x50 */  int m_bAcquired;
    /* 0x54 */  u32 m_State;
    /* 0x58 */  u8 m_Error;
    /* 0x59 */  u8 _pad1[3];
    /* 0x5C */  int m_StartID;
    /* 0x60 */  u8 UserBuffer[0x150];
    /* 0x1B0 */ u8 ArmRequest[0x1F8];
    /* 0x3A8 */ int ArmRequestNumber[7];
    /* 0x3C4 */ u32 ArmBufferAddr;
    /* 0x3C8 */ void *pBufDescToCancel[7];
    /* 0x400 */ int bFlushing[7];
    /* 0x41C */ void *pChannel[7];
    /* 0x454 */ u32 direction[7];
    /* 0x470 */ u32 FWDataType[7];
    /* 0x48C */ void *pArmMsgFifo[7];
    /* 0x4C4 */ int bDone;
    /* 0x4C8 */ u32 m_systemControl;
    /* 0x4CC */ u32 m_pictureResolution;
    /* 0x4D0 */ u32 m_inputControl;
    /* 0x4D4 */ u32 m_syncMode;
    /* 0x4D8 */ u32 m_rateControl;
    /* 0x4DC */ u32 m_rateControlEx;
    /* 0x4E0 */ u32 m_bitRate;
    /* 0x4E4 */ u32 m_filterControl;
    /* 0x4E8 */ u32 m_gopLoopFilter;
    /* 0x4EC */ u32 m_etControl;
    /* 0x4F0 */ u32 m_blkXferSize;
    /* 0x4F4 */ u32 m_outPictureResolution;
    /* 0x4F8 */ u32 m_audioControlParam;
    /* 0x4FC */ u32 m_audioControlExAAC;
    /* 0x500 */ u32 m_audioControlExG711;
    /* 0x504 */ u32 m_audioControlExLPCM;
    /* 0x508 */ u32 m_audioControlExSILK;
    /* 0x50C */ u32 m_EncIndexCapFreq;
    /* 0x510 */ u32 m_MP4VideoBlockNumber;
    /* 0x514 */ u8 m_audioEnhancement[0x28];
    /* 0x53C */ u32 m_EncStopMode;
    /* 0x540 */ int m_bEncEnableVidPadding;
    /* 0x544 */ int m_bEncVidFrozen;
    /* 0x548 */ int m_bEncVidStillInput;
    /* 0x54C */ u8 m_EncCapMode[0x14];
    /* 0x560 */ u32 m_EncMjpegQuality;
    /* 0x564 */ u32 m_EncMjpegFrameBuffer;
    /* 0x568 */ u8 m_ExternalTriggerToSync[0x10];
    /* 0x578 */ u8 m_PTSCounterReset[0x14];
    /* 0x58C */ u8 m_RawVideoDecimationFactor[0x14];
    /* 0x5A0 */ u32 m_DeinterlaceMode;
    /* 0x5A4 */ u32 m_LargeCompressBufferControl;
    /* 0x5A8 */ u32 m_EnableLowBitrateMode;
    /* 0x5AC */ u32 m_encFontTableAddr;
    /* 0x5B0 */ u32 m_encTextListAddr;
    /* 0x5B4 */ u32 m_encSyncTimeAddr;
    /* 0x5B8 */ u32 m_encVidBufLumaAddr;
    /* 0x5BC */ u32 m_encVidBufChromaAddr;
    /* 0x5C0 */ u32 m_encFrameLABufAddr;
    /* 0x5C4 */ u32 m_encTopLABufAddr;
    /* 0x5C8 */ u32 m_encBottomLABufAddr;
    /* 0x5CC */ u32 m_encVidBufPTS;
    /* 0x5D0 */ u32 m_streamID;
    /* 0x5D4 */ u32 m_outputMode;
    /* 0x5D8 */ u32 m_decodeResolution;
    /* 0x5DC */ u32 m_decStopMode;
    /* 0x5E0 */ u32 m_decStopDispMode;
    /* 0x5E4 */ u32 m_decPauseDispMode;
    /* 0x5E8 */ u32 m_decTrickPlaySpeed;
    /* 0x5EC */ u32 m_decTrickPlayDirection;
    /* 0x5F0 */ u32 m_decTrickFieldPreference;
    /* 0x5F4 */ u32 m_decDispMode;
    /* 0x5F8 */ u32 m_decDispTVSystem;
    /* 0x5FC */ u32 m_decDispInputSize;
    /* 0x600 */ u32 m_decDispOutputSize;
    /* 0x604 */ u32 m_decDispInputOrigin;
    /* 0x608 */ u32 m_decDispOutputOrigin;
    /* 0x60C */ u32 m_decFlushBufSelect;
    /* 0x610 */ u32 m_decXferMode;
    /* 0x614 */ u32 m_decXferMinimumSize;
    /* 0x618 */ u32 m_decFWSharedMemWr;
    /* 0x61C */ u32 m_decFWSharedMemRd;
    /* 0x620 */ u32 m_codec_function;
    /* 0x624 */ u32 m_video_input;
    /* 0x628 */ u32 m_video_in_ch;
    /* 0x62C */ u32 m_video_output;
    /* 0x630 */ u32 m_video_out_ch;
    /* 0x634 */ u32 m_audio_input;
    /* 0x638 */ u32 m_audio_in_ch;
    /* 0x63C */ u32 m_audio_output;
    /* 0x640 */ u32 m_audio_out_ch;
    /* 0x644 */ void *m_EvtReply;
};

/* ============================================
 * CTask (0x3498 bytes)
 * ============================================ */
struct c_task {
    /* 0x00 */   struct c_object m_Object;
    /* 0x38 */   struct c_thread m_Thread;

    /* 0x98 */   void *Alloc;
    /* 0xA0 */   void *Release;
    /* 0xA8 */   void *Open;
    /* 0xB0 */   void *Close;
    /* 0xB8 */   void *Start;
    /* 0xC0 */   void *Stop;
    /* 0xC8 */   void *Acquire;
    /* 0xD0 */   void *Pause;
    /* 0xD8 */   void *Resume;
    /* 0xE0 */   void *CancelBuffer;
    /* 0xE8 */   void *NewBuffer;
    /* 0xF0 */   void *Flush;
    /* 0xF8 */   void *FlushArm;
    /* 0x100 */  void *DMARequest;
    /* 0x108 */  void *CompleteData;
    /* 0x110 */  void *CompleteUser;
    /* 0x118 */  void *CompleteArm;
    /* 0x120 */  void *ProcessArmMessage;
    /* 0x128 */  void *ProcessIoComplete;
    /* 0x130 */  void *ProcessDataStreaming;
    /* 0x138 */  void *ProcessCancelBuffer;
    /* 0x140 */  void *ProcessFlush;
    /* 0x148 */  void *ProcessFlushArm;

    /* 0x150 */  struct cql_codec *m_pMpegCodec;
    /* 0x158 */  struct task_data m_TaskData[8];
    /* 0x33B8 */ struct task_dma_request m_dmaRequest;
    /* 0x33D4 */ u8 _pad1[4];
    /* 0x33D8 */ struct task_dma_request *m_pDmaRequest;
    /* 0x33E0 */ void *m_EvtDmaReqComplete;
    /* 0x33E8 */ struct task_io_pending m_ioPending[2];
    /* 0x3440 */ struct task_io_pending *m_pPending[2];
    /* 0x3450 */ struct c_object m_CritSectionIOPending;
    /* 0x3488 */ u32 m_dwMaxDMASize;
    /* 0x348C */ int m_StartID;
    /* 0x3490 */ u32 m_taskIdPCMOut;
    /* 0x3494 */ u32 m_taskIdAESOut;
};

/* ============================================
 * PCI_MEMORY_RANGE (0x18 bytes)
 * ============================================ */
struct pci_memory_range {
    /* 0x00 */ u32 Length;
    /* 0x04 */ u8 _pad[4];
    /* 0x08 */ u64 PhysicalAddress;
    /* 0x10 */ void *VirtualAddress;
};

/* ============================================
 * PCI_RW_RAM_STRUCT (0x39 bytes, pack(1))
 * ============================================ */
struct pci_rw_ram_struct {
    /* 0x00 */ u32 bar;
    /* 0x04 */ u16 transferSize;
    /* 0x06 */ u16 repeatCount;
    /* 0x08 */ u64 offset;
    /* 0x10 */ u64 data;
    /* 0x18 */ u64 transferTime;
    /* 0x20 */ u64 driverTime;
    /* 0x28 */ u32 TransferMode;
    /* 0x2C */ u32 PicWidthMB;
    /* 0x30 */ u32 PicHeightMB;
    /* 0x34 */ u32 DataSwap;
    /* 0x38 */ u8 IsFrameMode;
} __packed;

/* ============================================
 * _PCIE_DEVICE_EXTENSION (0x1788 bytes)
 * ============================================ */
struct pcie_device_extension {
    /* 0x00 */   void *pPCIeCntl;
    /* 0x08 */   void *FunctionalDeviceObject;
    /* 0x10 */   void *PhysicalDeviceObject;
    /* 0x18 */   void *LowerDeviceObject;
    /* 0x20 */   void *InterruptObject;
    /* 0x28 */   u8 InterfaceType;
    /* 0x29 */   u8 _pad1[3];
    /* 0x2C */   u32 BusType;
    /* 0x30 */   u32 BusNumber;
    /* 0x34 */   u8 InterruptLevel;
    /* 0x35 */   u8 _pad2[3];
    /* 0x38 */   u32 InterruptVector;
    /* 0x3C */   u8 _pad3[4];
    /* 0x40 */   u64 InterruptAffinity;
    /* 0x48 */   u32 InterruptMode;
    /* 0x4C */   u8 TimerStarted;
    /* 0x4D */   u8 _pad4[3];
    /* 0x50 */   void *pTimerDpc;
    /* 0x58 */   void *pTimer;
    /* 0x60 */   u8 StartEvent[0x18];
    /* 0x78 */   u8 StopEvent[0x18];
    /* 0x90 */   u8 RemoveEvent[0x18];
    /* 0xA8 */   u32 State;
    /* 0xAC */   u32 OutstandingIO;
    /* 0xB0 */   u32 InterruptStatus;
    /* 0xB4 */   u32 InterruptEnable;
    /* 0xB8 */   u8 Dpc[0x40];
    /* 0xF8 */   struct device_transfer DmaDevices[64];
    /* 0x12F8 */ int ReadDmaChannel;
    /* 0x12FC */ int WriteDmaChannel;
    /* 0x1300 */ u8 DmaBufferList[0x10];
    /* 0x1310 */ u64 DmaBufferListLock;
    /* 0x1318 */ u64 DmaQueueLock;
    /* 0x1320 */ u8 DmaQueueLockIrql;
    /* 0x1321 */ u8 _pad5[1];
    /* 0x1322 */ u16 m_dmaBar;
    /* 0x1324 */ u16 m_numBars;
    /* 0x1326 */ u16 m_CardConfigId;
    /* 0x1328 */ u16 m_NumDmaAvailable;
    /* 0x132A */ u16 m_InterruptAvailable;
    /* 0x132C */ u8 m_64BitAddress;
    /* 0x132D */ u8 m_64BitCapable;
    /* 0x132E */ u8 _pad6[2];
    /* 0x1330 */ u32 m_DmaMaxTransfer;
    /* 0x1334 */ u8 _pad7[4];
    /* 0x1338 */ struct pci_memory_range m_MemoryRange[6];
    /* 0x13C8 */ struct pci_memory_range m_IoRange[6];
    /* 0x1458 */ u8 m_pciConfig[0x100];
    /* 0x1558 */ u8 m_pciHeader[0x188];
    /* 0x16E0 */ struct pci_rw_ram_struct m_ReadInfo;
    /* 0x1719 */ struct pci_rw_ram_struct m_WriteInfo;
    /* 0x1752 */ u8 _pad8[6];
    /* 0x1758 */ void __iomem *pRegisters;
    /* 0x1760 */ void __iomem *pRegistersEx;
    /* 0x1768 */ u64 DmaStartTime;
    /* 0x1770 */ u64 IrpStartTime;
    /* 0x1778 */ void *pBusCallbackFunc;
    /* 0x1780 */ void *pBusCallbackContext;
};

/* ============================================
 * PED_DMA_ENGINE (0x100 bytes)
 * ============================================ */
struct ped_dma_engine {
    /* 0x00 */ u32 Capabilities;
    /* 0x04 */ u32 ControlStatus;
    /* 0x08 */ u64 Descriptor;
    /* 0x10 */ u32 HardwareTime;
    /* 0x14 */ u32 ChainCmplByteCount;
    /* 0x18 */ u32 Reserved[58];
};

/* ============================================
 * PED_DMA_DESCRIPTOR (0x20 bytes)
 * ============================================ */
struct ped_dma_descriptor {
    /* 0x00 */ u32 Control;
    /* 0x04 */ u32 ByteCount;
    /* 0x08 */ u64 SystemAddress;
    /* 0x10 */ u64 CardAddress;
    /* 0x18 */ u64 NextDescriptor;
};

/* ============================================
 * V4L2 buffer wrapper (Linux-specific)
 * ============================================ */
struct c985_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

/* ============================================
 * ICodecLib (0x68 bytes)
 * ============================================ */
struct i_codec_lib {
    /* 0x00 */ void *InitDevice;
    /* 0x08 */ void *Release;
    /* 0x10 */ void *Reset;
    /* 0x18 */ void *Set;
    /* 0x20 */ void *Get;
    /* 0x28 */ void *GetMpegCodec;
    /* 0x30 */ void *GetVideoDecoder;
    /* 0x38 */ void *GetVideoEncoder;
    /* 0x40 */ void *GetAudioCodec;
    /* 0x48 */ void *GetTuner;
    /* 0x50 */ void *GetTVAudio;
    /* 0x58 */ void *Disable;
    /* 0x60 */ void *Enable;
};

/* ============================================
 * _QPMPGCODEC_INITDATA (0xE0 = 224 bytes)
 * ============================================ */
struct qp_mpgcodec_initdata {
    u8 data[224];  /* TODO: break out individual fields if needed */
};

/* ============================================
 * _QPCODECLIB_INITDATA (0x120 = 288 bytes)
 * ============================================ */
struct qp_codeclib_initdata {
    /* 0x00 */  int bDownloadFW;
    /* 0x04 */  u8 _pad1[4];
    /* 0x08 */  struct qp_mpgcodec_initdata mpgCodecInitData;  /* 224 bytes */
    /* 0xE8 */  u8 vidDecoderInitData[12];
    /* 0xF4 */  u8 vidEncoderInitData[8];
    /* 0xFC */  u8 tunerInitData[4];
    /* 0x100 */ u8 tvAudioInitData[4];
    /* 0x104 */ u8 audCodecInitData[4];
    /* 0x108 */ u8 i2cInitData[12];
    /* 0x114 */ u8 i2cInitDataEx[12];
};

/* ============================================
 * CQLCodecLib (0x240 = 576 bytes)
 * ============================================ */
struct cql_codec_lib {
    /* 0x000 */ struct c_object m_Object;
    /* 0x038 */ struct i_codec_lib m_iCodecLib;
    /* 0x0A0 */ void *m_pDO;
    /* 0x0A8 */ void *m_PDOLayered;
    /* 0x0B0 */ struct qp_codeclib_initdata m_InitData;
    /* 0x1D0 */ void *m_pDeviceCallback;
    /* 0x1D8 */ void *m_callbackContext;
    /* 0x1E0 */ void *m_pUsbInterface;
    /* 0x1E8 */ void *m_pPCIeCntl;           /* CPCIeCntl* */
    /* 0x1F0 */ void *m_pUsbCntl;            /* CUsbCntl* */
    /* 0x1F8 */ struct cql_codec *m_pMpgCodec;
    /* 0x200 */ void *m_pI2CProvider;        /* CI2C* */
    /* 0x208 */ void *m_pI2CProviderEx;      /* CI2C* */
    /* 0x210 */ void *m_pVidDecoder;         /* CVideoDecoder* */
    /* 0x218 */ void *m_pVidEncoder;         /* CVideoEncoder* */
    /* 0x220 */ void *m_pAudCodec;           /* CAudioCodec* */
    /* 0x228 */ void *m_pTuner;              /* CTuner* */
    /* 0x230 */ void *m_pTVAudio;            /* CTVAudio* */
    /* 0x238 */ u32 m_dwDeviceError;
    /* 0x23C */ u8 _pad1[4];
};

/* ============================================
 * c985_poc - Main device structure (Linux-specific)
 * ============================================ */
struct c985_poc {
    /* Windows Driver Structures */
    struct pcie_device_extension pcie;
    struct cql_codec codec;

    /* Linux-specific: PCI */
    struct pci_dev *pdev;
    int irq_registered;

    /* Linux-specific: V4L2 */
    struct v4l2_device v4l2_dev;
    struct video_device vdev;
    struct vb2_queue vb2_queue;
    struct mutex v4l2_lock;
    struct list_head buf_list;
    spinlock_t buf_lock;
    unsigned int width;
    unsigned int height;
    unsigned int sequence;
    int hdmi_valid;

    /* Linux-specific: Frame handling */
    DECLARE_KFIFO_PTR(frame_fifo, u8);
    spinlock_t frame_lock;
    struct list_head pending_frames;
    struct work_struct frame_work;
    u32 frame_sequence;
    bool encoder_running;

    /* Linux-specific: IRQ/Work */
    spinlock_t irq_lock;
    struct work_struct irq_work;
    struct work_struct dma_work;

    /* Linux-specific: Completion/DMA tracking (temporary) */
    struct completion mailbox_complete;
    struct completion dma_done;
    u32 dma_interrupt_status;
    int dma_engine_idx[64];

    /* Firmware State (temporary?) */
    u32 qpsos_version;              /* QPSOS version from firmware header */
    u32 config_base;                /* Config area: 0x2F2000 (v2) or 0x0F2000 (v3+) */


    /* Linux-specific: Debug */
    struct dentry *debug_dir;

    /* C985-specific hardware config */
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
};


/* ============================================
 * Function Prototypes
 * ============================================ */

#define c985_bar0(d)        ((d)->pcie.pRegisters)
#define c985_bar1(d)        ((d)->pcie.pRegistersEx)
#define c985_hci(d)         (&(d)->codec.m_hci)
#define c985_task(d)        ((d)->codec.m_pTask)

#endif /* STRUCTS_H */
