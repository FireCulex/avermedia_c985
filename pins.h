/* SPDX-License-Identifier: GPL-2.0 */
/* pin.h — PCM Output Pin for AVerMedia C985 */
#ifndef PINS_H
#define PINS_H
#include "cdevice.h"

#include <linux/types.h>
typedef unsigned long  ulong;
typedef unsigned char  uchar;
typedef signed long long long64;

#include <linux/kernel.h>
#include "qperrors.h"
#include "include/abi/qp_buffer_descriptor.h"
#include "include/abi/_qp_ksstream_header.h"
#include "include/abi/averimagetype.h"
#include "include/abi/_ksstream_pointer.h"
#include "include/abi/cdatapin.h"
#include "include/abi/cyuvoutpin.h"
#include "include/abi/cpcmoutpin.h"

#include "v4l2.h"


/* ============================================
 * _TQP_GUID (16 bytes)
 * ============================================ */
struct _TQP_GUID {
    u32 Data1;                      /* 0x00 */
    u16 Data2;                      /* 0x04 */
    u16 Data3;                      /* 0x06 */
    u8 Data4[8];                    /* 0x08 */
};

typedef struct _TQP_GUID _TQP_GUID;

/* ============================================
 * _KSPIN_DISPATCH
 * ============================================ */
struct _KSPIN_DISPATCH {
    void *Create;
    void *Close;
    void *Process;
    void *Reset;
};

/* ============================================
 * KSAUTOMATION_TABLE
 * ============================================ */
struct KSAUTOMATION_TABLE {
    ulong PropertyHandlersCount;
    void *PropertyHandlers;
    ulong MethodHandlersCount;
    void *MethodHandlers;
    ulong EventHandlersCount;
    void *EventHandlers;
};

/* ============================================
 * QP_PCM_DATAFORMAT (0x14 = 20 bytes)
 * Windows struct from PDB
 * ============================================ */
struct QP_PCM_DATAFORMAT {
    u32 nChannels;            /* 0x00 */
    u32 nSamplesPerSec;       /* 0x04 */
    u32 nAvgBytesPerSec;      /* 0x08 */
    u32 nBlockAlign;          /* 0x0C */
    u32 nBitsPerSample;       /* 0x10 */
};                            /* Total: 0x14 = 20 bytes */

/* ============================================
 * KS_COMPRESSION Structure
 * ============================================ */
struct KS_COMPRESSION {
    u32 RatioNumerator;       /* 0x00 */
    u32 RatioDenominator;     /* 0x04 */
    u32 RatioConstantMargin;  /* 0x08 */
};                            /* Total: 0x0C = 12 bytes */

/* ============================================
 * KS_FRAMING_RANGE Structure
 * ============================================ */
struct KS_FRAMING_RANGE {
    u32 MinFrameSize;         /* 0x00 */
    u32 MaxFrameSize;         /* 0x04 */
    u32 Stepping;             /* 0x08 */
};                            /* Total: 0x0C = 12 bytes */

/* ============================================
 * KS_FRAMING_RANGE_WEIGHTED Structure
 * ============================================ */
struct KS_FRAMING_RANGE_WEIGHTED {
    struct KS_FRAMING_RANGE Range;  /* 0x00 */
    u32 FrameWeight;                /* 0x0C */
};                            /* Total: 0x10 = 16 bytes */

/* ============================================
 * KS_FRAMING_ITEM Structure
 * ============================================ */
struct KS_FRAMING_ITEM {
    u32 Frames;                       /* 0x00 */
    u32 PinFlags;                     /* 0x04 */
    struct KS_COMPRESSION OutputCompression;  /* 0x08 */
    u32 PinWeight;                    /* 0x14 */
    struct KS_FRAMING_RANGE_WEIGHTED FramingRange;  /* 0x18 */
    struct KS_FRAMING_RANGE PhysicalRange;  /* 0x28 */
};                            /* Total: 0x34 = 52 bytes */

/* ============================================
 * KSALLOCATOR_FRAMING_EX Structure
 * ============================================ */
struct KSALLOCATOR_FRAMING_EX {
    u32 CountItems;             /* 0x00 */
    u32 PinFlags;               /* 0x04 */
    struct KS_COMPRESSION OutputCompression;  /* 0x08 */
    u32 PinWeight;              /* 0x14 */
    struct KS_FRAMING_ITEM FramingItem[1];  /* 0x18 */
};                            /* Total: 0x50 + variable */

/* ============================================
 * KSDATAFORMAT Structure (partial)
 * ============================================ */
struct KSDATAFORMAT {
    u32 Size;                   /* 0x00 */
    u32 Flags;                  /* 0x04 */
    u32 SampleSize;             /* 0x08 */
    u32 Reserved;               /* 0x0C */
    u8 MajorFormat[16];         /* 0x10 */
    u8 SubFormat[16];           /* 0x20 */
    u8 Specifier[16];           /* 0x30 */
    /* Followed by format-specific data at 0x40+ */
};

/* ============================================
 * WAVEFORMATEX Structure (for audio format)
 * ============================================ */
struct WAVEFORMATEX {
    u16 wFormatTag;             /* 0x00 */
    u16 nChannels;              /* 0x02 */
    u32 nSamplesPerSec;         /* 0x04 */
    u32 nAvgBytesPerSec;        /* 0x08 */
    u16 nBlockAlign;            /* 0x0C */
    u16 wBitsPerSample;         /* 0x0E */
    u16 cbSize;                 /* 0x10 */
};

/* ============================================
 * KS_PIN_DESCRIPTOR Structure
 * ============================================ */
struct KS_PIN_DESCRIPTOR {
    void *AllocatorFraming;     /* 0x00 - KSALLOCATOR_FRAMING_EX * */
    /* ... other fields ... */
};


/* ============================================
 * RECT Structure
 * ============================================ */
struct _tagRECT {
    s32 left;                   /* 0x00 */
    s32 top;                    /* 0x04 */
    s32 right;                  /* 0x08 */
    s32 bottom;                 /* 0x0C */
};


/* ============================================
 * VTable Pointer
 * In Ghidra, this appears as: CPCMOutPin::`vftable'
 * Stored at offset 0x00 in CDataPin (_padding_[0])
 * ============================================ */
extern const u64 CPCMOutPin_vftable[];

/* ============================================
 * CDataQueue VTable - ADDED
 * ============================================ */
extern const u64 CDataQueue_vftable[];

/* ============================================
 * QUEUE_ENTRY_CPP Structure
 * ============================================ */
void *CPCMOutPin_CPCMOutPin(struct cpcm_out_pin *this,
                            struct _KSPIN *param_1,
                            struct c_device *param_2,
                            u32 param_3,
                            u32 param_4,
                            int param_5,
                            long *param_6);

/* CDataPin constructor */
void *CDataPin_CDataPin(struct c_data_pin *this,
                        struct _KSPIN *param_1,
                        struct c_device *param_2,
                        u32 param_3,
                        u32 param_4,
                        u32 param_5);

/* CDataPin::Create - creates queues and initializes buffers */
long CDataPin_Create(struct c_data_pin *this);

/* CDataPin::Process - processes data */
long CDataPin_Process(struct c_data_pin *this);

/* CBasePin constructor */
void *CBasePin_CBasePin(struct c_base_pin *this,
                        struct _KSPIN *param_1,
                        struct c_device *param_2,
                        u32 param_3,
                        u32 param_4,
                        u32 param_5);

/* CBasePin::initPin - initializes pin framing */
long CBasePin_InitPin(struct c_base_pin *this,
                      u32 param_1,
                      u32 param_2,
                      s64 param_3);

/* CBasePin::setFrameSize */
long CBasePin_SetFrameSize(struct c_base_pin *this, u32 width, u32 height);

/* Linux alternative to _KsEdit */
int ks_edit_allocator(void *bag, void **ptr, size_t old_size, size_t new_size, u32 tag);

/* GUID comparison */
int IsEqualTQPGUID(const void *guid1, const void *guid2);

/* CYUVOutPin constructor */
void *CYUVOutPin_CYUVOutPin(struct cyuv_out_pin *this,
                            struct _KSPIN *param_1,
                            struct c_device *param_2,
                            u32 param_3,
                            u32 param_4,
                            u32 param_5,
                            long *param_6);

/* FormatData constructor */
struct FormatData *FormatData_FormatData(struct FormatData *this);

/* AVerScreen initialization */
void InitialAVerScreen(void);

/* QUEUE_ENTRY_CPP constructor */
void QUEUE_ENTRY_CPP_constructor(void *entry);

/* Vector constructor iterator helper */
void vector_constructor_iterator(void *base, size_t elem_size, size_t count,
                                 void (*constructor)(void *));

void CBasePin_GetTaskHandle(struct c_base_pin *this);


/* CppQueue<>::GetOneEntry (vtable offset 0x20) - ADDED */
struct QUEUE_ENTRY_CPP *CppQueue_GetOneEntry(void *queue);

/* CppQueue<>::AddEntry (vtable offset 0x18) - ADDED */
void CppQueue_AddEntry(void *queue, struct QUEUE_ENTRY_CPP *entry);

long CDataPin_StreamCallback(struct c_data_pin *this, u32 param_1, void *param_2);

_EQPErrors CDataPin_onBufferComplete(struct c_data_pin *this,
                                     struct _QP_BUFFER_DESCRIPTOR *desc,
                                     struct _KSSTREAM_POINTER *stream_ptr);
struct _KSSTREAM_POINTER *CDataPin_getNextBuffer(struct c_data_pin *this);

_EQPErrors CBasePin_CodecOpen(struct c_base_pin *this,
                              struct IMpegCodec *codec);
void CDataPin_submitBuffer(struct c_data_pin *this);

void CDataPin_returnBuffer(struct c_data_pin *this,
                           struct _KSSTREAM_POINTER *ptr);

struct QUEUE_ENTRY_CPP *CppQueue_RemoveEntry(void *queue,
                                             struct QUEUE_ENTRY_CPP *entry);


void CDataPin_attachAVerMsg(struct c_data_pin *this,
                            struct _KSSTREAM_POINTER *stream_ptr,
                            enum AVerImageType image_type);

void CDataPin_buildScatterGatherList(struct c_data_pin *this,
                                     struct _QP_BUFFER_DESCRIPTOR *desc,
                                     struct _KSSTREAM_POINTER *stream_ptr);

void CDataPin_buildBufferDescriptor(struct c_data_pin *this,
                                    struct _QP_BUFFER_DESCRIPTOR *desc,
                                    struct _KSSTREAM_POINTER *stream_ptr);


long CBasePin_Create(struct c_base_pin *this);
u32 CBasePin_KsPtsToPts(struct c_base_pin *this, struct KSTIME *param_1);

#endif /* PINS_H */

