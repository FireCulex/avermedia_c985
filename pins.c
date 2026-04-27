/* SPDX-License-Identifier: GPL-2.0 */
/* pin.c — PCM Output Pin for AVerMedia C985 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#include "cdevice.h"
#include "pins.h"
#include "structs.h"
#include "types.h"
#include "cqlcodec.h"
#include "v4l2.h"
#include "sync.h"
#include "include/abi/qp_buffer_descriptor.h"
#include "cobject.h"
#include "ctask/ctask.h"
#include "include/abi/_ksstream_pointer_offset.h"
#include "include/abi/ccapturefilter.h"
#include "include/abi/_ksstream_pointer.h"
#include "include/abi/_qp_large_integer.h"
#include "include/abi/_ksmapping.h"
#include "queue.h"
#include "include/abi/cbasepin.h"
#include "include/abi/cdatapin.h"
#include "include/abi/cpcmoutpin.h"
#include "include/abi/cyuvoutpin.h"
#include <linux/delay.h>

#define SYNC_PRINT(fmt, ...) do { \
printk(KERN_EMERG "C985_HALT: " fmt "\n", ##__VA_ARGS__); \
mdelay(1000); \
} while (0)



void CBasePin_fillFrameInfo(struct c_base_pin *this,
                    struct _KSSTREAM_HEADER *header,
                    u32 data_used,
                    u32 options_flags);
void CBasePin_timeStamp(struct c_base_pin *this,
                        struct _KSSTREAM_HEADER *header,
                        u64 param_2,
                        u8 param_3,
                        _QP_BUFFER_DESCRIPTOR *desc);

#define KS_EDIT_TAG	0x4c585051  /* 'QPXL' in little-endian */

/* ============================================
 * Linux Alternative to _KsEdit
 *
 * Windows _KsEdit allocates/reallocates memory with tagging.
 * In Linux, we use krealloc/kzalloc with proper error handling.
 *
 * @bag: Allocator context (unused in Linux, but kept for compatibility)
 * @ptr: Pointer to pointer that will receive the allocated memory
 * @old_size: Previous allocation size (0 for new allocation)
 * @new_size: New allocation size
 * @tag: Allocation tag for debugging/tracking
 *
 * Returns: 0 on success, negative errno on failure
 * ============================================ */
int ks_edit_allocator(void *bag, void **ptr, size_t old_size, size_t new_size, u32 tag)
{
    void *new_ptr;

    /* Validate inputs */
    if (!ptr || new_size == 0)
        return -EINVAL;

    /*
     * In Linux kernel, we don't have the same allocator bag concept
     * as Windows Kernel Streaming. We use standard slab allocation.
     *
     * The tag parameter is kept for debugging purposes and can be
     * used with kernel memory debugging features.
     */

    if (old_size == 0 || *ptr == NULL) {
        /* New allocation - use kzalloc for zeroed memory */
        new_ptr = kzalloc(new_size, GFP_KERNEL);
        if (!new_ptr) {
            pr_debug("ks_edit_allocator: failed to allocate %zu bytes (tag 0x%08x)\n",
                     new_size, tag);
            return -ENOMEM;
        }
    } else {
        /* Reallocation - use krealloc */
        new_ptr = krealloc(*ptr, new_size, GFP_KERNEL);
        if (!new_ptr) {
            pr_debug("ks_edit_allocator: failed to reallocate %zu bytes (tag 0x%08x)\n",
                     new_size, tag);
            return -ENOMEM;
        }

        /* Zero out the new portion if growing */
        if (new_size > old_size)
            memset((char *)new_ptr + old_size, 0, new_size - old_size);
    }

    *ptr = new_ptr;

    pr_debug("ks_edit_allocator: allocated %zu bytes at %p (tag 0x%08x)\n",
             new_size, new_ptr, tag);

    return 0;
}

/* ============================================
 * QUEUE_ENTRY_CPP Constructor
 * ============================================ */
void QUEUE_ENTRY_CPP_constructor(void *entry)
{
    struct QUEUE_ENTRY_CPP *e = entry;

    if (e) {
        e->Data = NULL;
        e->pNext = NULL;
    }
}

/* ============================================
 * Vector Constructor Iterator
 * Constructs an array of objects by calling constructor on each
 * ============================================ */
void vector_constructor_iterator(void *base, size_t elem_size, size_t count,
                                 void (*constructor)(void *))
{
    size_t i;
    char *ptr = base;

    if (!base || !constructor || elem_size == 0)
        return;

    for (i = 0; i < count; i++) {
        constructor(ptr);
        ptr += elem_size;
    }
}

/* ============================================
 * CBasePin Constructor
 * Matches: CBasePin::CBasePin
 * ============================================ */
void *CBasePin_CBasePin(struct c_base_pin *this,
                        struct _KSPIN *param_1,
                        struct CDevice *param_2,
                        u32 param_3,
                        u32 param_4,
                        u32 param_5)
{
    if (!this)
        return NULL;

    /* Zero initialize the structure */
    memset(this, 0, sizeof(struct c_base_pin));

    /* Store KSPIN pointer */
    this->m_p_ks_pin = param_1;

    /* Store device pointer */
    this->m_pDevice = param_2;

    /* Store open type */
    this->m_dwOpenType = param_3;

    /* Initialize state */
    this->m_State = 0;  /* KSSTATE_STOP */
    this->m_EOS = 0;
    this->m_bDisabled = 0;

    /* Initialize counters */
    this->m_picture_num = 0;
    this->m_dropped_cnt = 0;
    this->m_start_time = 0;
    this->m_duration = 0;

    /* Initialize frame dimensions */
    this->m_dwFrameWidth = 0;
    this->m_dwFrameHeight = 0;

    /* Buffer alignment flags */
    this->m_bBufferPartialFill = 0;
    this->m_bBufferFrameAligned = 0;

    pr_debug("%s: CBasePin created at %p, KSPIN=%p, Device=%p\n",
             __func__, this, param_1, param_2);

    return this;
}

/* ============================================
 * CBasePin::initPin
 * Matches: CBasePin::initPin
 *
 * Initializes the pin's allocator framing structure.
 * This sets up the frame size, count, and constraints.
 * ============================================ */
long CBasePin_InitPin(struct c_base_pin *this,
                      u32 param_1, u32 param_2, s64 param_3)
{
    struct KSALLOCATOR_FRAMING_EX *pKVar1;

    this->m_frame_size = param_2;
    this->m_duration = param_3;

    /* DON'T reallocate the KSPIN - it's already writable on Linux */

    /* Only allocate AllocatorFraming if Descriptor exists */
    if (!this->m_p_ks_pin || !this->m_p_ks_pin->Descriptor) {
        pr_debug("CBasePin_InitPin: no KSPIN or Descriptor, skipping framing\n");
        return 0;
    }

    if (!this->m_p_ks_pin->Descriptor->AllocatorFraming) {
        this->m_p_ks_pin->Descriptor->AllocatorFraming =
        kzalloc(0x70, GFP_KERNEL);
        if (!this->m_p_ks_pin->Descriptor->AllocatorFraming)
            return -ENOMEM;
    }

    pKVar1 = this->m_p_ks_pin->Descriptor->AllocatorFraming;
    pKVar1->FramingItem[0].Frames = param_1;
    pKVar1->FramingItem[0].FramingRange.Range.MaxFrameSize = this->m_frame_size;
    pKVar1->FramingItem[0].FramingRange.Range.MinFrameSize = this->m_frame_size;
    pKVar1->FramingItem[0].PhysicalRange.MaxFrameSize = this->m_frame_size;
    pKVar1->FramingItem[0].PhysicalRange.MinFrameSize = this->m_frame_size;
    pKVar1->FramingItem[0].FramingRange.Range.Stepping = 0;
    pKVar1->FramingItem[0].PhysicalRange.Stepping = 0;

    return 0;
}


/* ============================================
 * CDataPin Constructor
 * Matches: CDataPin::CDataPin
 * ============================================ */
void *CDataPin_CDataPin(struct c_data_pin *this,
                        struct _KSPIN *param_1,
                        struct CDevice *param_2,
                        u32 param_3,
                        u32 param_4,
                        u32 param_5)
{
    if (!this)
        return NULL;

    /* Call base constructor first */
    CBasePin_CBasePin((struct c_base_pin *)this,
                      param_1, param_2, param_3, param_4, param_5);

    /*
     * Set vtable pointer at offset 0x00 (_padding_[0])
     * In Ghidra this is: CPCMOutPin::`vftable'
     */
    this->base._padding_[0] = (u64)CPCMOutPin_vftable;

    /*
     * Initialize the m_Entries array (256 QUEUE_ENTRY_CPP elements)
     * Each element is 0x18 bytes
     */
    vector_constructor_iterator(this->m_Entries,
                                sizeof(struct QUEUE_ENTRY_CPP),
                                256,
                                QUEUE_ENTRY_CPP_constructor);

    /* Zero the pointer arrays */
    memset(this->m_StreamSRBs, 0, sizeof(this->m_StreamSRBs));
    memset(this->m_BufDesces, 0, sizeof(this->m_BufDesces));
    memset(this->m_DataBufferArray, 0, sizeof(this->m_DataBufferArray));

    /* Initialize queue pointers */
    this->m_pFreeQueue = NULL;
    this->m_pDataRequestQueue = NULL;

    /* Store device pointer (also set by base constructor) */
    this->m_pDevice = param_2;

    /* Initialize counters */
    this->m_dwDropCounter = 0;
    this->m_bIsFirstFrame = 1;

    pr_debug("%s: CDataPin created at %p\n", __func__, this);

    return this;
}


/* ============================================
 * CPCMOutPin Constructor
 * Matches: CPCMOutPin::CPCMOutPin
 *
 * Creates a PCM output pin for audio streaming.
 * Determines buffer size based on sample rate.
 * ============================================ */
void *CPCMOutPin_CPCMOutPin(struct cpcm_out_pin *this,
                            struct _KSPIN *param_1,
                            struct CDevice *param_2,
                            u32 param_3,
                            u32 param_4,
                            int param_5,
                            long *param_6)
{
    struct KSDATAFORMAT *pKVar2;
    long lVar3;
    struct QP_PCM_DATAFORMAT *pvVar4;
    u32 local_28;
    struct QP_PCM_DATAFORMAT *local_18;
    u32 local_10;

    if (!this || !param_1 || !param_6) {
        if (param_6)
            *param_6 = -EINVAL;
        return this;
    }

    /* Call CDataPin constructor with type=6 (PCM audio) */
    CDataPin_CDataPin(&this->base, param_1, param_2, param_3, param_4, 6);

    /* Initialize PCM-specific fields */
    this->m_pMpegCodec = NULL;
    this->m_bWaveInFilter = param_5;

    /* Get ConnectionFormat from KSPIN */
    pKVar2 = param_1->ConnectionFormat;

    /* Default buffer size for 44100 Hz */
    local_28 = 0x800;  /* 2048 bytes */

    /* Initialize padding field */
    this->base.base._padding_[16] = 0;

    /* Get sample rate from format structure (offset 0x44 in KSDATAFORMAT) */
    local_10 = *(u32 *)((char *)pKVar2 + 0x44);

    /* Adjust buffer size based on sample rate */
    switch (local_10) {
        case 16000:
            local_28 = 0x600;  /* 1536 bytes */
            break;
        case 24000:
            local_28 = 0x900;  /* 2304 bytes */
            break;
        case 32000:
            local_28 = 0xC00;  /* 3072 bytes */
            break;
        case 44100:  /* 0xAC44 */
            local_28 = 0x1000; /* 4096 bytes */
            break;
        case 48000:
            local_28 = 0x1200; /* 4608 bytes */
            break;
        default:
            pr_debug("CPCMOutPin: Unsupported sample rate %u Hz\n", local_10);
            *param_6 = -ENOTSUPP;
            return this;
    }

    /* Only allocate format structure if no prior error */
    if (*param_6 >= 0) {
        /* Allocate QP_PCM_DATAFORMAT structure (0x14 = 20 bytes) */
        pvVar4 = kzalloc(sizeof(struct QP_PCM_DATAFORMAT), GFP_KERNEL);
        local_18 = pvVar4;
        this->base.base._padding_[16] = (u64)pvVar4;

        if (!this->base.base._padding_[16]) {
            pr_debug("CPCMOutPin: failed to alloc QP_PCM_DATAFORMAT\n");
            *param_6 = -ENOMEM;
            return this;
        }

        /* Copy 5 format fields from KSDATAFORMAT */
        local_18->nChannels = *(u16 *)((char *)pKVar2 + 0x42);
        local_18->nSamplesPerSec = *(u32 *)((char *)pKVar2 + 0x44);
        local_18->nAvgBytesPerSec = *(u32 *)((char *)pKVar2 + 0x48);
        local_18->nBlockAlign = *(u16 *)((char *)pKVar2 + 0x4C);
        local_18->nBitsPerSample = *(u16 *)((char *)pKVar2 + 0x4E);

        pr_debug("CPCMOutPin: QP_PCM_DATAFORMAT Channels=%d SampleRate=%u "
        "AvgBytesPerSec=%u BlockAlign=%u BitsPerSample=%u\n",
        local_18->nChannels,
        local_18->nSamplesPerSec,
        local_18->nAvgBytesPerSec,
        local_18->nBlockAlign,
        local_18->nBitsPerSample);

        /* Initialize base pin with framing parameters */
        /* Cast c_data_pin to c_base_pin (base fields are at the beginning) */
        lVar3 = CBasePin_InitPin((struct c_base_pin *)&this->base.base, 0x40, local_28, 0);
        *param_6 = lVar3;
    }

    pr_debug("CPCMOutPin: Created at %p, buffer size %u bytes\n",
             this, local_28);

    return this;
}

/* ============================================
 * VTable
 * In Ghidra: CPCMOutPin::`vftable'
 * This is the virtual function table for CPCMOutPin class.
 * Each entry is a function pointer. The vtable is stored
 * at offset 0x00 in the CDataPin base class (_padding_[0]).
 * ============================================ */
const u64 CPCMOutPin_vftable[] = {
    /* CBasePin vtable entries (inherited) */
    /* 0x00 */ 0,  /* scalar deleting destructor */
    /* 0x08 */ 0,  /* `vector deleting destructor' */
    /* Add actual function addresses as you recover them from Ghidra */
};


/* ============================================
 * VTable for CYUVOutPin
 * In Ghidra: CYUVOutPin::`vftable'
 * ============================================ */
const u64 CYUVOutPin_vftable[] = {
    /* CBasePin vtable entries (inherited) */
    /* 0x00 */ 0,
    /* Add actual function addresses as you recover them from Ghidra */
};

/* ============================================
 * Global AVerScreen buffers
 * ============================================ */
static u8 *m_pAVerScreen_Y;
static u8 *m_pAVerScreen_UV;

/* ============================================
 * GUIDs for YUV format detection
 * ============================================ */
static const u8 _GUID_32315659_f072_40ca_829d_47d5d2835422[16] = {
    0x59, 0x56, 0x31, 0x32, 0x72, 0xf0, 0xca, 0x40,
    0x82, 0x9d, 0x47, 0xd5, 0xd2, 0x83, 0x54, 0x22
};

static const u8 _GUID_30323449_0000_0010_8000_00aa00389b71[16] = {
    0x49, 0x34, 0x32, 0x30, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static const u8 _GUID_56555951_0000_0010_8000_00aa00389b71[16] = {
    0x51, 0x59, 0x55, 0x56, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};

static const u8 _GUID_32315659_0000_0010_8000_00aa00389b71[16] = {
    0x59, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
};


/* ============================================
 * FormatData Constructor
 * ============================================ */
struct FormatData *FormatData_FormatData(struct FormatData *this)
{
    if (!this)
        return NULL;

    memset(this, 0, sizeof(struct FormatData));

    return this;
}

/* ============================================
 * InitialAVerScreen - Initialize screen buffers
 * ============================================ */
void InitialAVerScreen(void)
{
    if (!m_pAVerScreen_Y) {
        m_pAVerScreen_Y = vzalloc(1920 * 1080);  /* Y plane */
        if (!m_pAVerScreen_Y)
            pr_err("CYUVOutPin: Failed to alloc Y plane\n");
    }

    if (!m_pAVerScreen_UV) {
        m_pAVerScreen_UV = vzalloc(1920 * 1080 / 2);  /* UV plane */
        if (!m_pAVerScreen_UV)
            pr_err("CYUVOutPin: Failed to alloc UV plane\n");
    }

    pr_debug("CYUVOutPin: AVerScreen initialized Y=%p UV=%p\n",
             m_pAVerScreen_Y, m_pAVerScreen_UV);
}

/* ============================================
 * CBasePin::setFrameSize
 * ============================================ */
long CBasePin_SetFrameSize(struct c_base_pin *this, u32 width, u32 height)
{
    if (!this)
        return -EINVAL;

    this->m_dwFrameWidth = width;
    this->m_dwFrameHeight = height;

    pr_debug("CBasePin::setFrameSize: %dx%d\n", width, height);

    return 0;
}


/* ============================================
 * CYUVOutPin Constructor
 * Matches: CYUVOutPin::CYUVOutPin
 * param_5 is hardcoded to 5 (video pin type)
 * ============================================ */
void *CYUVOutPin_CYUVOutPin(struct cyuv_out_pin *this,
                            struct _KSPIN *param_1,
                            struct CDevice *param_2,
                            u32 param_3,
                            u32 param_4,
                            u32 param_5,
                            long *param_6)
{
    int iVar3;
    long lVar2;
    u8 *puVar9;
    struct KSDATAFORMAT *pKVar8;
    void *local_98;
    u8 local_78[88];  /* 0x58 = 88 bytes for KS_VIDEOINFOHEADER */
    int i;

    if (!this || !param_1 || !param_6) {
        if (param_6)
            *param_6 = -EINVAL;
        return this;
    }

    /* Call CDataPin constructor with hardcoded type=5 (video) */
    CDataPin_CDataPin(&this->base, param_1, param_2, param_3, param_4, 5);
    this->base.base._padding_[0] = (u64)CYUVOutPin_vftable;

    /* Set vtable pointer */
    this->base.base._padding_[0] = (u64)CYUVOutPin_vftable;

    /* Initialize format structure */
    FormatData_FormatData(&this->m_format);

    /* Copy connection format data (0x58 = 88 bytes)
     * Skip first 8 bytes of KSDATAFORMAT (Size, Flags, SampleSize, Reserved)
     */
    pKVar8 = param_1->ConnectionFormat;
    puVar9 = local_78;
    for (i = 0; i < 88; i++) {
        puVar9[i] = ((u8 *)pKVar8)[i + 8];
    }

    /* Copy to m_info_hdr */
    memcpy(&this->m_info_hdr, local_78, 88);

    /* Copy format data from m_info_hdr to m_format */
    this->m_format.m_image_height = this->m_info_hdr.bmiHeader.biHeight;
    this->m_format.m_image_width = this->m_info_hdr.bmiHeader.biWidth;
    this->m_format.m_bit_count = this->m_info_hdr.bmiHeader.biBitCount;

    /* Initialize base pin with framing: 10 frames, image size, frame time */
    lVar2 = CBasePin_InitPin((struct c_base_pin *)this,
                             10,
                             this->m_info_hdr.bmiHeader.biSizeImage,
                             this->m_info_hdr.AvgTimePerFrame);
    *param_6 = lVar2;

    if (*param_6 >= 0) {
        /* Set stream header size */
        param_1->StreamHeaderSize = 0x80;

        /* Allocate FormatData (0x14 = 20 bytes) */
        local_98 = kzalloc(sizeof(struct FormatData), GFP_KERNEL);
        this->base.base._padding_[16] = (u64)local_98;
        if (!this->base.base._padding_[16]) {
            pr_err("CYUVOutPin: Failed to alloc FormatData\n");
            *param_6 = -ENOMEM;
            return this;
        }

        /* Determine YUV format by comparing SubFormat GUID */
        iVar3 = IsEqualTQPGUID(&param_1->ConnectionFormat->SubFormat,
                               _GUID_32315659_f072_40ca_829d_47d5d2835422);
        if (iVar3 == 0) {
            iVar3 = IsEqualTQPGUID(&param_1->ConnectionFormat->SubFormat,
                                   _GUID_30323449_0000_0010_8000_00aa00389b71);
            if (iVar3 == 0) {
                iVar3 = IsEqualTQPGUID(&param_1->ConnectionFormat->SubFormat,
                                       _GUID_56555951_0000_0010_8000_00aa00389b71);
                if (iVar3 == 0) {
                    iVar3 = IsEqualTQPGUID(&param_1->ConnectionFormat->SubFormat,
                                           _GUID_32315659_0000_0010_8000_00aa00389b71);
                    if (iVar3 == 0) {
                        /* Unknown format */
                        ((struct FormatData *)this->base.base._padding_[16])->m_compression = 3;
                    } else {
                        /* YV12 */
                        ((struct FormatData *)this->base.base._padding_[16])->m_compression = 1;
                    }
                } else {
                    /* YUY2 */
                    ((struct FormatData *)this->base.base._padding_[16])->m_compression = 0;
                }
            } else {
                /* I420 */
                ((struct FormatData *)this->base.base._padding_[16])->m_compression = 2;
            }
        } else {
            /* YV12 */
            ((struct FormatData *)this->base.base._padding_[16])->m_compression = 1;
        }

        /* Fill in QP_YUV_DATAFORMAT fields (matching Windows decompilation)
         * Windows writes to this->_padding_ which is the vtable pointer location
         * _padding_[0] = biWidth, _padding_[1] = biHeight, _padding_[2] = biBitCount, _padding_[3] = frameRate
         * _padding_[4] = dataType (from GUID comparison)
         */
        ((struct FormatData *)this->base.base._padding_[16])->m_image_width =
        this->m_info_hdr.bmiHeader.biWidth;
        ((struct FormatData *)this->base.base._padding_[16])->m_image_height =
        this->m_info_hdr.bmiHeader.biHeight;
        ((struct FormatData *)this->base.base._padding_[16])->m_bit_count =
        this->m_info_hdr.bmiHeader.biBitCount;
        ((struct FormatData *)this->base.base._padding_[16])->m_stride_in_bytes = 30;

        /* Match Windows debug print exactly:
         * "%s QP_YUV_DATAFORMAT w(%d) h(%d) bits(%d) fr(%d) dadaType(%d)\n"
         */
        pr_debug("CYUVOutPin::CYUVOutPin QP_YUV_DATAFORMAT w(%d) h(%d) bits(%d) fr(%d) dadaType(%d)\n",
                 this->m_info_hdr.bmiHeader.biWidth,
                 this->m_info_hdr.bmiHeader.biHeight,
                 this->m_info_hdr.bmiHeader.biBitCount,
                 30,  /* Default frame rate (local_b8 = 0x1e) */
                 ((struct FormatData *)this->base.base._padding_[16])->m_compression);
    }

    /* Initialize AVerScreen buffers if needed */
    if (!m_pAVerScreen_Y || !m_pAVerScreen_UV)
        InitialAVerScreen();

    /* Set frame size in base pin */
    CBasePin_SetFrameSize((struct c_base_pin *)this,
                          this->m_info_hdr.bmiHeader.biWidth,
                          this->m_info_hdr.bmiHeader.biHeight);

    pr_debug("CYUVOutPin: Created at %p, format %dx%d\n",
             this,
             this->m_info_hdr.bmiHeader.biWidth,
             this->m_info_hdr.bmiHeader.biHeight);

    return this;
}


 /* /* ============================================
 * CBasePin::Create
 * Matches: CBasePin::Create
 * ============================================ */
 long CBasePin_Create(struct c_base_pin *this)
 {
     this->m_bDisabled = 0;

     /* In the decompilation, this->_padding_ is the CDevice pointer
      *      because CBasePin was constructed with CDevice* as param_2 */
     CDevice_AddPin((struct CDevice *)this->m_pDevice, this);

     return 0;
 }
 /* ============================================
  * GUIDs from GetTaskHandle decompilation
  * ============================================ */
 static const u8 _GUID_6da2460e_3021_425f_9dc5_7311a8aeb761[16] = {
     0x0e, 0x46, 0xa2, 0x6d, 0x21, 0x30, 0x5f, 0x42,
     0x9d, 0xc5, 0x73, 0x11, 0xa8, 0xae, 0xb7, 0x61
 };

 static const u8 _GUID_f01ab56e_d77b_43cd_8d6c_6817e06a651f[16] = {
     0x6e, 0xb5, 0x1a, 0xf0, 0x7b, 0xd7, 0xcd, 0x43,
     0x8d, 0x6c, 0x68, 0x17, 0xe0, 0x6a, 0x65, 0x1f
 };

 static const u8 _GUID_fb6c4281_0353_11d1_905f_0000c0cc16ba[16] = {
     0x81, 0x42, 0x6c, 0xfb, 0x53, 0x03, 0xd1, 0x11,
     0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba
 };

 static const u8 _GUID_d29be036_e7e2_4bee_b7ff_6d02b32fcc69[16] = {
     0x36, 0xe0, 0x9b, 0xd2, 0xe2, 0xe7, 0xee, 0x4b,
     0xb7, 0xff, 0x6d, 0x02, 0xb3, 0x2f, 0xcc, 0x69
 };

 /* ============================================
  * IsEqualTQPGUID
  * ============================================ */
 int IsEqualTQPGUID(const void *guid1, const void *guid2)
 {
     const u8 *g1 = guid1;
     const u8 *g2 = guid2;
     int i;

     if (!guid1 || !guid2)
         return 0;

     for (i = 0; i < 16; i++) {
         if (g1[i] != g2[i])
             return 0;
     }
     return 1;
 }


 /* ============================================
  * CBasePin::GetTaskHandle
  * Matches decompilation exactly
  * ============================================ */
 void CBasePin_GetTaskHandle(struct c_base_pin *this)
 {
     void *parent_filter;
     void *filter_obj;
     u32 filter_type;
     int guid_match;
     u32 task_handle;
     struct _KSPIN *ks_pin;
     struct _KSPIN_DESCRIPTOR_EX *desc;

     if (!this || !this->m_p_ks_pin)
         return;

     ks_pin = this->m_p_ks_pin;

     /* KsPinGetParentFilter - get parent from KSPIN Context */
     parent_filter = KsPinGetParentFilter(ks_pin);
     if (!parent_filter) {
         pr_err("CBasePin::GetTaskHandle: Failed to get parent filter\n");
         return;
     }

     /* Get CppObject at offset 0x10 of parent filter */
     filter_obj = *(void **)((char *)parent_filter + 0x10);

     /* CppObject::WhoAmI */
     filter_type = CppObject_WhoAmI(filter_obj);

     pr_debug("CBasePin::GetTaskHandle: filter=0x%p, type=0x%x\n", parent_filter, filter_type);

     if (filter_type == 0x103ea) {
         /* CEncoderFilter */
         desc = ks_pin->Descriptor;

         guid_match = IsEqualTQPGUID(desc->PinDescriptor.Name, _GUID_6da2460e_3021_425f_9dc5_7311a8aeb761);
         if (guid_match) {
             task_handle = CEncoderFilter_GetEncodeTaskHandle(filter_obj);
             this->m_hTask = task_handle;
             pr_debug("CBasePin::GetTaskHandle: MPEG out pin, hTask=%d\n", task_handle);
             return;
         }

         guid_match = IsEqualTQPGUID(desc->PinDescriptor.Name, _GUID_f01ab56e_d77b_43cd_8d6c_6817e06a651f);
         if (guid_match) {
             task_handle = CEncoderFilter_GetEncodeTaskHandle(filter_obj);
             this->m_hTask = task_handle;
             pr_debug("CBasePin::GetTaskHandle: MPEG out pin (alt), hTask=%d\n", task_handle);
             return;
         }

         guid_match = IsEqualTQPGUID(desc->PinDescriptor.Name, _GUID_fb6c4281_0353_11d1_905f_0000c0cc16ba);
         if (guid_match) {
             task_handle = CEncoderFilter_GetEncodeTaskHandle(filter_obj);
             this->m_hTask = task_handle;
             pr_debug("CBasePin::GetTaskHandle: YUV input pin, hTask=%d\n", task_handle);
             return;
         }

         guid_match = IsEqualTQPGUID(desc->PinDescriptor.Name, _GUID_d29be036_e7e2_4bee_b7ff_6d02b32fcc69);
         if (guid_match) {
             task_handle = CCaptureFilter_GetRawAudioTaskHandle(filter_obj);
             this->m_hTask = task_handle;
             pr_debug("CBasePin::GetTaskHandle: PCM pin, hTask=%d\n", task_handle);
             return;
         }

         /* Default: Raw Video */
         task_handle = CCaptureFilter_GetRawVideoTaskHandle(filter_obj);
         this->m_hTask = task_handle;
         pr_debug("CBasePin::GetTaskHandle: YUV pin (default), hTask=%d\n", task_handle);

     } else if (filter_type == 0x103fc) {
         desc = ks_pin->Descriptor;

         guid_match = IsEqualTQPGUID(desc->PinDescriptor.Name, _GUID_6da2460e_3021_425f_9dc5_7311a8aeb761);
         if (guid_match) {
             task_handle = CEncoderFilter_GetEncodeTaskHandle(filter_obj);
             this->m_hTask = task_handle;
             pr_debug("CBasePin::GetTaskHandle: MPEG out pin (type 0x103fc), hTask=%d\n", task_handle);
         }
     } else {
         pr_err("CBasePin::GetTaskHandle: Unknown filter type 0x%x\n", filter_type);
     }

     pr_debug("CBasePin::GetTaskHandle: hTask=%d\n", this->m_hTask);
 }

/* ============================================
   * CppQueue<>::GetOneEntry
   * Vtable offset 0x20
   * ============================================ */
struct QUEUE_ENTRY_CPP *CppQueue_GetOneEntry(void *queue)
{
    if (!queue)
        return NULL;

    pr_debug("CppQueue::GetOneEntry: queue=%p\n", queue);
    return NULL;
}

/* ============================================
   * CppQueue<>::AddEntry
   * Vtable offset 0x18
   * ============================================ */
void CppQueue_AddEntry(void *queue, struct QUEUE_ENTRY_CPP *entry)
 {
     if (!queue || !entry)
         return;

     /* Actual queue implementation */
     pr_debug("CppQueue::AddEntry: queue=%p, entry=%p\n", queue, entry);
 }

 /* ============================================
  * CDataPin::Create
  * Matches decompilation exactly
  * ============================================ */
 long CDataPin_Create(struct c_data_pin *this)
 {
     struct c_data_queue *pFreeQueue;
     struct c_data_queue *pDataRequestQueue;
     struct _QP_BUFFER_DESCRIPTOR *pBufDesc;
     struct IMpegCodec *codec;
     unsigned long i;
     typedef long (*codec_fn)(struct IMpegCodec *, ulong);

     SYNC_PRINT("CDP1 CDataPin_Create ENTER this=%px", this);

     pr_debug("%s: CDataPin::Create Enter\n", __func__);

     /* Call CBasePin::GetTaskHandle (vtable offset 0x58) */
     CBasePin_GetTaskHandle((struct c_base_pin *)this);
     SYNC_PRINT("CDP2 GetTaskHandle done");

     /* Create m_pFreeQueue */
     pFreeQueue = kzalloc(0x90, GFP_KERNEL);
     SYNC_PRINT("CDP3 pFreeQueue kzalloc=%px", pFreeQueue);

     if (pFreeQueue) {
         pFreeQueue = CDataQueue_CDataQueue(pFreeQueue, (struct CppObject *)this, 0x10406, 1, NULL);
     }
     this->m_pFreeQueue = pFreeQueue;
     SYNC_PRINT("CDP4 m_pFreeQueue=%px", this->m_pFreeQueue);

     if (!this->m_pFreeQueue) {
         pr_err("%s: Unable to create CDataQueue (m_pFreeQueue)!\n", __func__);
         return -ENOMEM;
     }

     /* Create m_pDataRequestQueue */
     pDataRequestQueue = kzalloc(0x90, GFP_KERNEL);
     SYNC_PRINT("CDP5 pDataRequestQueue kzalloc=%px", pDataRequestQueue);

     if (pDataRequestQueue) {
         pDataRequestQueue = CDataQueue_CDataQueue(pDataRequestQueue, (struct CppObject *)this, 0x10406, 1, NULL);
     }
     this->m_pDataRequestQueue = pDataRequestQueue;
     SYNC_PRINT("CDP6 m_pDataRequestQueue=%px", this->m_pDataRequestQueue);

     if (!this->m_pDataRequestQueue) {
         pr_err("%s: Unable to create CDataQueue (m_pDataRequestQueue)!\n", __func__);
         kfree(this->m_pFreeQueue);
         this->m_pFreeQueue = NULL;
         return -ENOMEM;
     }

     SYNC_PRINT("CDP7 about to init 256 entries");

     /* Initialize 256 stream SRBs and entries */
     for (i = 0; i < 0x100; i++) {
         pBufDesc = &this->m_BufDesces[i];
         memset(pBufDesc, 0, sizeof(struct _QP_BUFFER_DESCRIPTOR));

         pBufDesc->DataBufferArray = &this->m_DataBufferArray[i];

         this->m_StreamSRBs[i].pSrb = NULL;
         this->m_StreamSRBs[i].pBufDesc = &this->m_BufDesces[i];
         this->m_StreamSRBs[i].dwId = i;

         this->m_Entries[i].pNext = NULL;
         this->m_Entries[i].Data = &this->m_StreamSRBs[i];

         /* Vtable call at offset 0x18: CppQueue<>::AddEntry */
         CppQueue_AddEntry(this->m_pFreeQueue, &this->m_Entries[i]);

         if (i == 0 || i == 255)
             SYNC_PRINT("CDP8 entry[%lu] done", i);
     }

     SYNC_PRINT("CDP9 all 256 entries done");

     /* First run codec handling */
     if (this->m_pDevice && this->m_pDevice->m_FirstRun) {
         SYNC_PRINT("CDP10 first run codec path");
         codec = (struct IMpegCodec *)this->m_pDevice->m_pMpegCodec;

         if (codec) {
             CBasePin_CodecOpen((struct c_base_pin *)this, codec);

             if (this->base._padding_[0] != (u64)-1) {
                 CQLCodec_Stop(codec, (ulong)this->base._padding_[0]);
             }

             if (this->base._padding_[0] != (u64)-1) {
                 CQLCodec_Close(codec, (ulong)this->base._padding_[0]);
                 this->base._padding_[0] = (u64)-1;
             }
         }
     }

     SYNC_PRINT("CDP11 CDataPin_Create COMPLETE");

     pr_debug("%s: CDataPin::Create Complete\n", __func__);

     return 0;
 }

 /* ============================================
  * CDataPin::StreamCallback
  * Matches decompilation exactly
  *
  * param_1 = command code
  * param_2 = buffer descriptor pointer
  * ============================================ */
 long CDataPin_StreamCallback(struct c_data_pin *this, u32 param_1, void *param_2)
 {
     struct QUEUE_ENTRY_CPP *pQVar3;
     struct _KSSTREAM_POINTER *p_Var1;
     long ret;

     if (param_1 < 0x10001) {

         if (param_1 == 0x10000) {
             /* QPMPGCODEC_CMD_DONE_DATA */
             pr_debug("%s Info : QPMPGCODEC_CMD_DONE_DATA (%d)\n", __func__, param_1);

             /* Find the queue entry matching this buffer descriptor */
             pQVar3 = CDataQueue_GetEntryByBufDesc(this->m_pDataRequestQueue, param_2);
             if (!pQVar3)
                 return QPERR_SUCCESS;

             /* Get the stream pointer from the SRB */
             p_Var1 = pQVar3->Data->pSrb;

             pr_debug("%s(%d) : MPGCODEC_CMD_DONE_DATA id(%d) pBufDesc(0x%p) strmPtr(0x%p)\n",
                      __func__, param_1,
                      pQVar3->Data->dwId,
                      param_2,
                      p_Var1);

             /* Call onBufferComplete (vtable offset 0x68) */
             ret = CDataPin_onBufferComplete(this, param_2, p_Var1);

             /* Free scatter gather list if present (param_2 + 0x38) */
             if (*(u64 *)((char *)param_2 + 0x38) != 0) {
                 kfree(*(void **)((char *)param_2 + 0x38));
                 *(u64 *)((char *)param_2 + 0x38) = 0;
             }

             /* Free DMA buffer if present (param_2 + 0x18) */
             if (*(u64 *)((char *)param_2 + 0x18) != 0) {
                 kfree(*(void **)((char *)param_2 + 0x18));
                 *(u64 *)((char *)param_2 + 0x18) = 0;
             }

             /* Clear SRB pointer */
             pQVar3->Data->pSrb = NULL;

             /* Return entry to free queue (vtable offset 0x18 = CppQueue::AddEntry) */
             CppQueue_AddEntry(this->m_pFreeQueue, pQVar3);

             return ret;
         }

         if (param_1 == 0x10) {
             /* QPMPGCODEC_CMD_DEC_COMPLETE */
             pr_debug("%s Info : QPMPGCODEC_CMD_DONE_DATA (%d)\n", __func__, param_1);
             pr_debug("%s QPMPGCODEC_CMD_DEC_COMPLETE\n", __func__);

             /* Call decComplete (vtable offset 0x50) - verified stub, no action */
             return QPERR_SUCCESS;
         }

         if (param_1 == 0x100) {
             /* QPMPGCODEC_CMD_BUFFER_OVERFLOW */
             pr_debug("%s Info : QPMPGCODEC_CMD_BUFFER_OVERFLOW (%d)\n", __func__, param_1);
             return QPERR_SUCCESS;
         }

         if (param_1 == 0x200) {
             /* QPMPGCODEC_CMD_STOP_COMPLETED */
             pr_debug("%s Info : QPMPGCODEC_CMD_STOP_COMPLETED (%d)\n", __func__, param_1);
             return QPERR_SUCCESS;
         }

     } else {

         if (param_1 == 0x20000) {
             pr_debug("%s Info : QPMPGCODEC_CMD_DONE_DATA (%d)\n", __func__, param_1);
             return QPERR_SUCCESS;
         }

         if (param_1 == 0x40000) {
             pr_debug("%s Info : QPMPGCODEC_CMD_DONE_DATA (%d)\n", __func__, param_1);
             return QPERR_SUCCESS;
         }

         if (param_1 == 0x100000) {
             pr_debug("%s Info : QPMPGCODEC_CMD_DONE_DATA (%d)\n", __func__, param_1);
             return QPERR_SUCCESS;
         }
     }

     return QPERR_NOTIMPL;
 }

  _EQPErrors CDataPin_onBufferComplete(struct c_data_pin *this,
                                      struct _QP_BUFFER_DESCRIPTOR *desc,
                                      struct _KSSTREAM_POINTER *stream_ptr)
 {
     _EQPErrors ret;
     struct _KSSTREAM_POINTER *clone_ptr;
     struct project_manager *proj_mgr;
     int status_flag;
     u8 is_active;
     u64 callback_result;
     u8 local_buf[8];
     struct _KSPIN *ks_pin;

     /*
      * Access m_p_ks_pin at offset 0xa0 (from CBasePin inheritance)
      * Can't use this->m_p_ks_pin until header is updated
      */
     ks_pin = *(struct _KSPIN **)((char *)this + 0xa0);

     /* Get the first clone stream pointer */
     clone_ptr = KsPinGetFirstCloneStreamPointer(ks_pin);
     if (!clone_ptr) {
         pr_debug("%s NO CLONE POINTER p_buffer(0x%x)\n", __func__, desc);
         return QPERR_SUCCESS;
     }

     /* Verify clone pointer matches */
     if (clone_ptr != stream_ptr) {
         pr_debug("%s(%d) - p_clone(0x%x) != p_buffer(0x%x)\n",
                  __func__, __LINE__, clone_ptr, stream_ptr);
         return QPERR_FAIL;
     }

     /* Copy data used from buffer descriptor to stream header */
     clone_ptr->StreamHeader->DataUsed = desc->DataBufferArray->DataUsed;

     /* Check if pin is active */
     is_active = (ks_pin != NULL) ? 1 : 0;

     /* Direct call to CDataPin::Process (vtable 0x28) */
     if (ks_pin == NULL) {
         callback_result = 0;
     }
     else {
         callback_result = CDataPin_Process(this);
     }

     /* Direct call to CBasePin::timeStamp (vtable 0x48) */
     CBasePin_timeStamp((struct c_base_pin *)this, clone_ptr->StreamHeader, callback_result, is_active, desc);

     /* Direct call to CBasePin::fillFrameInfo (vtable 0x40) */
     CBasePin_fillFrameInfo((struct c_base_pin *)this, clone_ptr->StreamHeader,
                            desc->DataBufferArray->DataUsed,
                            desc->DataBufferArray->OptionsFlags);

     pr_debug("%s info : DataUsed : %d \n", __func__, desc->DataBufferArray->DataUsed);

     /* Check HDCP status via ProjectManager */
     /*
     status_flag = 0;
     proj_mgr = CDevice_getProjectManager(this->m_pDevice);
     ProjectManager_CheckHDCPStatus(proj_mgr, 3, &status_flag);

     if (status_flag == 1) {
         proj_mgr = CDevice_getProjectManager(this->m_pDevice);
         ProjectManager_SetHDCPFlag(proj_mgr, 4); */

         /* Check HDCP flag at offset 0xc10c */
         /* if (*(int *)((char *)this + 0xc10c) != 1) {
             CDataPin_attachAVerMsg(this, clone_ptr, IMAGE_HDCP);
         }
     } */

     /* Cleanup */
     KsStreamPointerUnlock(clone_ptr, 0);
     KsStreamPointerDelete(clone_ptr);

     /* Use ks_pin for AttemptProcessing */
     KsPinAttemptProcessing(ks_pin, 1);

     return QPERR_SUCCESS;
 }
void CBasePin_fillFrameInfo(struct c_base_pin *this,
                            struct _KSSTREAM_HEADER *header,
                            u32 data_used,
                            u32 options_flags)
 {
     header->DataUsed = data_used;
     header->OptionsFlags = header->OptionsFlags | options_flags;
     this->m_picture_num = this->m_picture_num + 1;
     header->TypeSpecificFlags = options_flags & 0xf000000;

     pr_debug("%s() OptionsFlags(0x%08x) TypeSpecificFlags(0x%08x)\n",
              __func__, header->OptionsFlags, header->TypeSpecificFlags);
 }

void CBasePin_timeStamp(struct c_base_pin *this,
                        struct _KSSTREAM_HEADER *header,
                        u64 param_2,
                        u8 param_3,
                        _QP_BUFFER_DESCRIPTOR *desc)
 {
     pr_debug("%s info : (%d) m_picture_num = %d , time = %lu\n",
              __func__, this->m_picture_num, param_2);

     if (this->m_picture_num == 0) {
         this->m_start_time = param_2;
     }

     header->Duration = this->m_duration;
     header->PresentationTime.Numerator = 1;
     header->PresentationTime.Denominator = 1;
     header->OptionsFlags = 0;
     header->OptionsFlags = header->OptionsFlags | 0x100;

     if ((desc->ulFlags & 0x40000) == 0) {
         if (param_3 == 0) {
             header->PresentationTime.Time = 0;
         }
         else {
             header->OptionsFlags = header->OptionsFlags | 0x10;
             header->PresentationTime.Time = param_2;
         }
     }
     else {
         header->OptionsFlags = header->OptionsFlags | 0x10;
         header->PresentationTime.Time = (desc->ulPTS * 10000) / 0x5a + this->m_start_time;
     }

     if (this->m_discontinuity != 0) {
         header->OptionsFlags = header->OptionsFlags | 4;
         this->m_discontinuity = 0;
     }

     if ((header->OptionsFlags & 0x10) != 0) {
         pr_debug("%s time(%lu:%lu) pts(%lu:%lu) KS time(%lu:%lu)\n",
                  __func__,
                  header->PresentationTime.Time,
                  header->PresentationTime.Numerator,
                  desc->ulPTS,
                  desc->ulPTS,
                  header->PresentationTime.Time,
                  header->PresentationTime.Denominator);
     }
 }

 long CDataPin_Process(struct c_data_pin *this)
 {
     pr_debug("%s Enter\n", __func__);
     CDataPin_submitBuffer(this);
     return 0x103;
 }

 void CDataPin_submitBuffer(struct c_data_pin *this)
 {
     struct PIN_DATA_REQ *pPVar1;
     int iVar2;
     struct _KSSTREAM_POINTER *p_Var3;
     struct IMpegCodec *pIVar4;
     struct IMpegCodec *pIVar5;
     struct QUEUE_ENTRY_CPP *local_58;
     short local_40[8];
     struct _QP_SCATTER_GATHER *local_30;
     struct project_manager *local_28;

     pr_debug("%s Enter \n", __func__);

     if ((((int)this->base._padding_[0] == -1) || ((int)this->base._padding_[0] == 0)) ||
         (((*(u32 *)(this->base._padding_[0] + 0x68) & 0x10000) != 0 &&
         ((int)this->base._padding_[0] != 3)))) {
         return;
         }

         local_58 = CppQueue_GetOneEntry(this->m_pFreeQueue);

     do {
         while (1) {
             if (local_58 == NULL) {
                 return;
             }

             if (*(int *)((char *)&this->base._padding_[1] + 4) != 0) {
                 return;
             }

             p_Var3 = CDataPin_getNextBuffer(this);
             if (p_Var3 == NULL) {
                 pr_debug("%s(%d) - getNextBuffer() no more\n", __func__, __LINE__);
                 local_58->Data->pSrb = NULL;
                 CppQueue_AddEntry(this->m_pFreeQueue, local_58);
                 return;
             }

             CDataPin_buildBufferDescriptor(this, local_58->Data->pBufDesc, p_Var3);

             iVar2 = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                    (_TQP_GUID *)&_GUID_fb6c4281_0353_11d1_905f_0000c0cc16ba);

             if ((iVar2 != 0) && (this->m_bIsFirstFrame == 1))
                 break;

             LAB_0001c32e:
             CDataPin_buildScatterGatherList(this, local_58->Data->pBufDesc, p_Var3);

             local_58->Data->pSrb = p_Var3;

             CppQueue_AddEntry(this->m_pDataRequestQueue, local_58);

             pr_debug("%s(%d) - AddBuffer id(%d) (0x%X)\n", __func__, __LINE__,
                      local_58->Data->dwId, local_58->Data);

             pPVar1 = local_58->Data;
             pIVar4 = CDevice_getMpegCodec(this->m_pDevice);
             pIVar5 = CDevice_getMpegCodec(this->m_pDevice);

             iVar2 = CQLCodec_AddBuffer(pIVar4, (ulong)this->base._padding_[0], pPVar1->pBufDesc);

             if (iVar2 < 0) {
                 pr_debug("%s(%d) - AddBuffer id(%d) (0x%X) FAILED!!!!\n", __func__, __LINE__,
                          local_58->Data->dwId, local_58->Data);

                 if (local_58->Data->pBufDesc->ScatterGatherBuffer != NULL) {
                     local_30 = local_58->Data->pBufDesc->ScatterGatherBuffer;
                     kfree(local_30);
                     local_58->Data->pBufDesc->ScatterGatherBuffer = NULL;
                 }

                 CppQueue_RemoveEntry(this->m_pDataRequestQueue, local_58);

                 local_58->Data->pSrb = NULL;

                 CppQueue_AddEntry(this->m_pFreeQueue, local_58);

                 CDataPin_returnBuffer(this, p_Var3);
                 return;
             }

             local_58 = CppQueue_GetOneEntry(this->m_pFreeQueue);
         }

         this->m_bIsFirstFrame = 0;

         local_28 = CDevice_getProjectManager(this->m_pDevice);

         /* ProjectManager vtable 0x28 is _purecall (stub) - skip or call directly */
         /* ProjectManager_CallStub(local_28, 4, local_40); */
         local_40[0] = 0;  /* Default return value from stub */

         if (local_40[0] == 0) {
             CDataPin_attachAVerMsg(this, p_Var3, IMAGE_NO_SIGNAL);
             local_58->Data->pBufDesc->DataBufferArray->DataUsed =
             (u32)((int)this->base._padding_[0] * *(int *)((char *)&this->base._padding_[1] + 4) * 3) / 2;

             CDataPin_onBufferComplete(this, local_58->Data->pBufDesc, p_Var3);
         }
         else {
             if (local_40[0] != 1)
                 goto LAB_0001c32e;

             CDataPin_attachAVerMsg(this, p_Var3, IMAGE_OUT_OF_RANGE);
             local_58->Data->pBufDesc->DataBufferArray->DataUsed =
             (u32)((int)this->base._padding_[0] * *(int *)((char *)&this->base._padding_[1] + 4) * 3) / 2;

             CDataPin_onBufferComplete(this, local_58->Data->pBufDesc, p_Var3);
         }
     } while (1);
 }

 struct _KSSTREAM_POINTER *CDataPin_getNextBuffer(struct c_data_pin *this)
 {
     struct _KSSTREAM_POINTER *local_28;
     struct _KSSTREAM_POINTER *local_20;
     int local_18;

     local_20 = (struct _KSSTREAM_POINTER *)KsPinGetLeadingEdgeStreamPointer(
         (void *)(uintptr_t)this->base._padding_[0], 1);

     if (local_20 == NULL) {
         pr_debug("%s - KsPinGetLeadingEdgeStreamPointer() == NULL\n", __func__);
         local_28 = NULL;
     }
     else {
         local_28 = NULL;
         local_18 = KsStreamPointerClone(local_20, 0, 0, &local_28);
         if (local_18 < 0) {
             KsStreamPointerUnlock(local_20, 0);
             pr_debug("%s - KsStreamPointerClone() FAILED(0x%x)\n", __func__, local_18);
             local_28 = NULL;
         }
         else {
             KsStreamPointerUnlock(local_20, 0);
             KsStreamPointerAdvance(local_20);
         }
     }
     return local_28;
 }

 _EQPErrors CBasePin_CodecOpen(struct c_base_pin *this,
                               struct IMpegCodec *codec)
 {
     _EQPErrors ret = QPERR_SUCCESS;

     pr_debug("%s pMpegCodec->Open task(%d)\n", __func__, this->m_hTask);

     if ((this->m_bDisabled == 0) && (this->m_hStreamLib == 0xffffffff)) {
         ret = CQLCodec_Open(codec,
                             this->m_hTask,
                             this->m_dwOpenType,
                             this->m_pOpenFormat,
                             &this->m_hStreamLib,
                             CDataPin_StreamCallback,
                             this);

         if (ret < 0) {
             this->m_hStreamLib = 0xffffffff;
             pr_debug("%s pMpegCodec->Open failed(%d)\n", __func__, ret);
         }
         else {
             pr_debug("%s pMpegCodec->Open task(%d) codec handle/m_hStreamLib (%d)\n",
                      __func__, this->m_hTask, this->m_hStreamLib);
         }
     }

     return ret;
 }

 void CDataPin_returnBuffer(struct c_data_pin *this,
                            struct _KSSTREAM_POINTER *ptr)
 {
     ptr->StreamHeader->DataUsed = 0;
     KsStreamPointerUnlock(ptr, 0);
     KsStreamPointerDelete(ptr);
 }

 #define AVERMSG_WIDTH   400
 #define AVERMSG_HEIGHT  300
 #define AVERMSG_SIZE    (AVERMSG_WIDTH * AVERMSG_HEIGHT)

 struct QUEUE_ENTRY_CPP *CppQueue_RemoveEntry(void *queue,
                                              struct QUEUE_ENTRY_CPP *entry)
 {
     u8 irql;
     struct QUEUE_ENTRY_CPP *cur;
     struct QUEUE_ENTRY_CPP *prev = NULL;

     /* Get head of queue (offset 0x70) */
     cur = *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x70);

     /* Check synchronization type (offset 0x18) */
     if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
         /* Semaphore/mutex-based synchronization */
         if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
             KeWaitForSingleObject((char *)queue + 0x38, 0, 0, 0, 0);
         }
     }
     else {
         /* Spinlock-based synchronization */
         irql = KeAcquireSpinLockRaiseToDpc((char *)queue + 0x28);
         *(u8 *)((char *)queue + 0x30) = irql;
     }

     /* Search for the entry in the linked list */
     do {
         if (cur == NULL) {
             /* Entry not found - release lock and return NULL */
             if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
                 if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
                     KeReleaseMutex((char *)queue + 0x38, 0);
                 }
             }
             else {
                 KeReleaseSpinLock((char *)queue + 0x28,
                                   *(u8 *)((char *)queue + 0x30));
             }
             return cur;
         }

         if (cur == entry) {
             /* Found the entry - remove it from the list */
             if (prev == NULL) {
                 /* Entry is at head of queue */
                 *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x70) = cur->pNext;

                 /* If queue is now empty, update tail too */
                 if (*(u64 *)((char *)queue + 0x70) == 0) {
                     *(u64 *)((char *)queue + 0x78) = *(u64 *)((char *)queue + 0x70);
                 }
             }
             else {
                 /* Entry is in middle or end of queue */
                 prev->pNext = cur->pNext;

                 /* If entry was at tail, update tail pointer */
                 if (cur->pNext == NULL) {
                     *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x78) = prev;
                 }
             }

             /* Decrement queue count (offset 0x80) */
             *(int *)((char *)queue + 0x80) = *(int *)((char *)queue + 0x80) - 1;

             /* Release lock and return NULL (matches decompile behavior) */
             if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
                 if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
                     KeReleaseMutex((char *)queue + 0x38, 0);
                 }
             }
             else {
                 KeReleaseSpinLock((char *)queue + 0x28,
                                   *(u8 *)((char *)queue + 0x30));
             }
             return NULL;
         }

         /* Move to next entry */
         prev = cur;
         cur = cur->pNext;
     } while (1);
 }

 static u8 avermsg_hdcp[AVERMSG_SIZE];
 static u8 avermsg_no_signal[AVERMSG_SIZE];
 static u8 avermsg_out_of_range[AVERMSG_SIZE];

 u8 *CYUVOutPin_m_pAVerMsg[3] = {
     avermsg_hdcp,
     avermsg_no_signal,
     avermsg_out_of_range,
 };

 /* Full frame replacements - sized for max resolution */
 static u8 averscreen_y[1920 * 1080];
 static u8 averscreen_uv[1920 * 1080 / 2];

 u8 *CYUVOutPin_m_pAVerScreen_Y = averscreen_y;
 u8 *CYUVOutPin_m_pAVerScreen_UV = averscreen_uv;

 void CDataPin_attachAVerMsg(struct c_data_pin *this,
                             struct _KSSTREAM_POINTER *stream_ptr,
                             enum AVerImageType image_type)
 {
     struct CppObject *cpp_obj;
     struct CEncoderFilter *enc_filter;
     struct CCaptureFilter *cap_filter;
     u8 *src_ptr;
     u32 frame_size;
     u32 frame_extent;
     enum QP_PROCESS_TYPE process_name;
     int guid_match;
     u32 width, height;
     u64 copy_count;
     u8 *src_y, *src_uv, *dst_ptr;
     int line_offset;
     u64 line_count;
     int i;

     pr_debug("%s Info : attach the avermedia message (%d)\n", __func__);

     /* Get parent filter from KS pin */
     void *parent_filter = KsPinGetParentFilter(this->base.m_p_ks_pin);
     cpp_obj = *(struct CppObject **)((char *)parent_filter + 0x10);

     u32 filter_type = CppObject_WhoAmI(cpp_obj);

     if (filter_type == 0x103fc) {
         /* Encoder filter path */
         enc_filter = *(struct CEncoderFilter **)((char *)parent_filter + 0x10);
         if (!enc_filter) {
             pr_debug("%s Error : can't get the encoder filter\n", __func__);
             return;
         }

         process_name = CEncoderFilter_getProcessName(enc_filter);

         if ((process_name != PROCESS_TYPE_AVER_RECENTRAL) &&
             (process_name != PROCESS_TYPE_AVER_DEMOAP) &&
             (process_name != PROCESS_TYPE_AVER_SDK)) {
             pr_debug("%s Warning : not the trust process. block the encoded stream\n", __func__);

         guid_match = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                     (_TQP_GUID *)&_GUID_6da2460e_3021_425f_9dc5_7311a8aeb761);
         if (guid_match != 0) {
             stream_ptr->StreamHeader->DataUsed = 0;
         }
             }
     }
     else if (filter_type == 0x103ea) {
         /* Capture filter path */
         cap_filter = *(struct CCaptureFilter **)((char *)parent_filter + 0x10);
         if (!cap_filter) {
             pr_debug("%s Error : can't get the capture filter\n", __func__);
             return;
         }

         /* Set process name based on device */
         if (this->m_pDevice->m_processname == PROCESS_TYPE_AVER_SDK) {
             CCaptureFilter_ProcessName_Setting(cap_filter, PROCESS_TYPE_AVER_SDK);
         }
         else if (this->m_pDevice->m_processname == PROCESS_TYPE_3RD_PARTY_AP) {
             CCaptureFilter_ProcessName_Setting(cap_filter, PROCESS_TYPE_3RD_PARTY_AP);
         }

         if (image_type == IMAGE_HDCP) {
             guid_match = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                         (_TQP_GUID *)&_GUID_fb6c4281_0353_11d1_905f_0000c0cc16ba);
             if (guid_match == 0) {
                 guid_match = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                             (_TQP_GUID *)&_GUID_d29be036_e7e2_4bee_b7ff_6d02b32fcc69);
                 if (guid_match != 0) {
                     process_name = CEncoderFilter_getProcessName((struct CEncoderFilter *)cap_filter);
                     if ((process_name != PROCESS_TYPE_AVER_RECENTRAL) &&
                         (process_name != PROCESS_TYPE_AVER_DEMOAP) &&
                         (process_name != PROCESS_TYPE_AVER_SDK)) {
                         pr_debug("%s Enter HDCP Screen Audio \n", __func__);
                     stream_ptr->StreamHeader->DataUsed = 0;
                         }
                 }
             }
             else {
                 process_name = CEncoderFilter_getProcessName((struct CEncoderFilter *)cap_filter);
                 if ((process_name != PROCESS_TYPE_AVER_RECENTRAL) &&
                     (process_name != PROCESS_TYPE_AVER_DEMOAP) &&
                     (process_name != PROCESS_TYPE_AVER_SDK) &&
                     (process_name != PROCESS_TYPE_3RD_PARTY_AP)) {
                     pr_debug("%s Enter HDCP Screen Video\n", __func__);

                 CppObject_enterCritical((struct CppObject *)this->m_pDevice);

                 src_ptr = stream_ptr->StreamHeader->Data;
                 width = (u32)this->base._padding_[0];
                 height = *(u32 *)((char *)&this->base._padding_[1] + 4);
                 frame_size = width * height;
                 frame_extent = stream_ptr->StreamHeader->FrameExtent;

                 /* Copy Y plane */
                 src_y = CYUVOutPin_m_pAVerScreen_Y;
                 dst_ptr = src_ptr;
                 for (copy_count = frame_size; copy_count != 0; copy_count--) {
                     *dst_ptr++ = *src_y++;
                 }

                 /* Copy UV plane */
                 src_uv = CYUVOutPin_m_pAVerScreen_UV;
                 dst_ptr = src_ptr + frame_size;
                 for (copy_count = frame_size / 2; copy_count != 0; copy_count--) {
                     *dst_ptr++ = *src_uv++;
                 }

                 if (frame_extent < 180000) {
                     pr_debug("%s Error : Not enough memory allocation to attach aver message\n", __func__);
                 }
                 else {
                     line_offset = ((height - 300) / 2) * width;
                     for (i = 0; i < 300; i++) {
                         if (src_ptr && CYUVOutPin_m_pAVerMsg[0]) {
                             u8 *msg_ptr = CYUVOutPin_m_pAVerMsg[0] + (i * 400);
                             u8 *dst_line = src_ptr + line_offset + ((width - 400) / 2);
                             for (line_count = 400; line_count != 0; line_count--) {
                                 *dst_line++ = *msg_ptr++;
                             }
                         }
                         line_offset += width;
                     }
                 }

                 CppObject_leaveCritical((struct CppObject *)this->m_pDevice);
                     }
             }
         }
         else if ((image_type < IMAGE_NO_SIGNAL) || (image_type > IMAGE_OUT_OF_RANGE)) {
             pr_debug("%s Error : no match type to attach\n", __func__);
         }
         else {
             guid_match = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                         (_TQP_GUID *)&_GUID_fb6c4281_0353_11d1_905f_0000c0cc16ba);
             if (guid_match == 0) {
                 guid_match = IsEqualTQPGUID(*(_TQP_GUID **)(this->base._padding_[0] + 0x50),
                                             (_TQP_GUID *)&_GUID_d29be036_e7e2_4bee_b7ff_6d02b32fcc69);
                 if (guid_match != 0) {
                     stream_ptr->StreamHeader->DataUsed = 0;
                 }
             }
             else {
                 CppObject_enterCritical((struct CppObject *)this->m_pDevice);

                 src_ptr = stream_ptr->StreamHeader->Data;
                 width = (u32)this->base._padding_[0];
                 height = *(u32 *)((char *)&this->base._padding_[1] + 4);
                 frame_size = width * height;
                 frame_extent = stream_ptr->StreamHeader->FrameExtent;

                 /* Copy Y plane */
                 src_y = CYUVOutPin_m_pAVerScreen_Y;
                 dst_ptr = src_ptr;
                 for (copy_count = frame_size; copy_count != 0; copy_count--) {
                     *dst_ptr++ = *src_y++;
                 }

                 /* Copy UV plane */
                 src_uv = CYUVOutPin_m_pAVerScreen_UV;
                 dst_ptr = src_ptr + frame_size;
                 for (copy_count = frame_size / 2; copy_count != 0; copy_count--) {
                     *dst_ptr++ = *src_uv++;
                 }

                 if (frame_extent < 120000) {
                     pr_debug("%s Error : Not enough memory allocation\n", __func__);
                 }
                 else {
                     line_offset = ((height - 300) / 2) * width;
                     for (i = 0; i < 300; i++) {
                         if (src_ptr && CYUVOutPin_m_pAVerMsg[image_type]) {
                             u8 *msg_ptr = CYUVOutPin_m_pAVerMsg[image_type] + (i * 400);
                             u8 *dst_line = src_ptr + line_offset + ((width - 400) / 2);
                             for (line_count = 400; line_count != 0; line_count--) {
                                 *dst_line++ = *msg_ptr++;
                             }
                         }
                         line_offset += width;
                     }
                 }

                 CppObject_leaveCritical((struct CppObject *)this->m_pDevice);
             }
         }
     }
     else {
         pr_debug("%s Error : it can't be executed with non-capture filter\n", __func__);
     }
 }
 void CDataPin_buildScatterGatherList(struct c_data_pin *this,
                                      struct _QP_BUFFER_DESCRIPTOR *desc,
                                      struct _KSSTREAM_POINTER *stream_ptr)
 {
     u64 alloc_size;
     struct _QP_SCATTER_GATHER *sg_buffer;
     u32 mapping_count;
     u32 i;

     mapping_count = 0;

     /* Check if scatter-gather is enabled (vtable flag at 0x68) */
     if ((*(u32 *)(this->base._padding_[0] + 0x68) & 0x100) != 0) {
         if (stream_ptr->Offset &&
             stream_ptr->StreamHeader->FrameExtent != 0 &&
             stream_ptr->Offset->Count != 0) {

             mapping_count = stream_ptr->Offset->Count;

         alloc_size = (u64)0x10 * mapping_count;
         sg_buffer = (struct _QP_SCATTER_GATHER *)kmalloc(alloc_size, GFP_KERNEL);

         desc->ScatterGatherBuffer = sg_buffer;

         if (!desc->ScatterGatherBuffer) {
             mapping_count = 0;
         }
         else {
             for (i = 0; i < mapping_count; i++) {
                 desc->ScatterGatherBuffer[i].PhysicalAddress =
                 *(_QP_LARGE_INTEGER *)(stream_ptr->Offset->u.Data + (u64)i * 0x10);
                 desc->ScatterGatherBuffer[i].ByteCount =
                 stream_ptr->Offset->u.Mappings[i].ByteCount;
                 desc->ScatterGatherBuffer[i].Alignment =
                 stream_ptr->Offset->u.Mappings[i].Alignment;
             }
         }
             }

             pr_debug("%s - mapping_count(%d)\n", __func__, mapping_count);
     }

     desc->NumberOfScatterGatherElements = mapping_count;
 }

  void CDataPin_buildBufferDescriptor(struct c_data_pin *this,
                                     struct _QP_BUFFER_DESCRIPTOR *desc,
                                     struct _KSSTREAM_POINTER *stream_ptr)
 {
     u32 pts_value;

     desc->ulFlags = 0;

     /* Check for ENDOFSTREAM flag */
     if ((stream_ptr->StreamHeader->OptionsFlags & 0x200) != 0) {
         pr_debug("%s KSSTREAM_HEADER_OPTIONSF_ENDOFSTREAM\n", __func__);
         *(u32 *)((char *)&this->base._padding_[1] + 4) = 1;
         desc->ulFlags = 0x20000;
     }

     /* Handle PTS (Presentation Time Stamp) */
     if ((stream_ptr->StreamHeader->OptionsFlags & 0x10) == 0) {
         desc->ulPTS = 0;
     }
     else {
         desc->DataBufferArray->PresentationTime.Time =
         stream_ptr->StreamHeader->PresentationTime.Time;
         desc->DataBufferArray->PresentationTime.Numerator =
         stream_ptr->StreamHeader->PresentationTime.Numerator;
         desc->DataBufferArray->PresentationTime.Denominator =
         stream_ptr->StreamHeader->PresentationTime.Denominator;

         desc->ulFlags |= 0x40000;

         pts_value = CBasePin_KsPtsToPts((struct c_base_pin *)this,
                                         &stream_ptr->StreamHeader->PresentationTime);
         desc->ulPTS = (u64)pts_value;
         desc->ulPTS = desc->ulPTS % 0x1ffffffff;

         pr_debug("%s KSSTREAM_HEADER_OPTIONSF_TIMEVALID PTS(%lu:%lu)\n", __func__,
                  desc->ulPTS, desc->ulPTS);
     }

     /* Copy stream header fields to buffer descriptor */
     desc->DataBufferArray->Duration = stream_ptr->StreamHeader->Duration;
     desc->DataBufferArray->FrameExtent = stream_ptr->StreamHeader->FrameExtent;
     desc->DataBufferArray->DataUsed = stream_ptr->StreamHeader->DataUsed;
     desc->DataBufferArray->Data = stream_ptr->StreamHeader->Data;
     desc->DataBufferArray->OptionsFlags = stream_ptr->StreamHeader->OptionsFlags;

     desc->NumberOfBuffers = 1;
     desc->ulBufferIndex = 0;
     desc->ulBufferOffset = 0;
     desc->ulBufferSize = stream_ptr->StreamHeader->FrameExtent;
     desc->ulDMABufferIndex = 0;
     desc->ulDMABufferOffset = 0;
     desc->ulTotalUsed = 0;

     /* Handle frame size mismatch */
     if (((int)this->base._padding_[0] == 2) &&
         ((u32)this->base._padding_[0] < stream_ptr->StreamHeader->FrameExtent)) {
         desc->DataBufferArray->FrameExtent = (u32)this->base._padding_[0];
     desc->ulBufferSize = (u32)this->base._padding_[0];
         }

         /* Set active flag if pin is valid */
         if ((int)this->base._padding_[0] != 0) {
             desc->ulFlags |= 0x10000;
         }

         /* Set EOS flag if detected */
         if (*(u32 *)((char *)&this->base._padding_[1] + 4) != 0) {
             desc->ulFlags |= 0x80000;
         }

         desc->Status = 1;
         desc->pBuffer = NULL;
         desc->unAlignedNumberBytes = 0;
 }
 /* In pins.c - add implementation */
 u32 CBasePin_KsPtsToPts(struct c_base_pin *this, struct KSTIME *param_1)
 {
     u32 result;
     s64 scaled_time;

     if (!param_1 || param_1->Denominator == 0)
         return 0;

     scaled_time = (param_1->Time * (u64)param_1->Numerator) /
     (s64)(u64)param_1->Denominator;
     result = (u32)((scaled_time * 0x5a) / 10000);

     return result;
 }

 /* Matches CDevice::AddPin */
 void CDevice_AddPin(struct CDevice *this, struct c_base_pin *pin)
 {
     if (!this || !this->m_pPinsMgr || !pin)
         return;

     /* Calls CppObjectMgr<CBasePin>::AddObject */
     CppObjectMgr_AddObject((struct CObjectMgr *)this->m_pPinsMgr, pin);
 }


 MODULE_DESCRIPTION("AVerMedia C985 PCM Output Pin Driver");
 MODULE_LICENSE("GPL v2");
