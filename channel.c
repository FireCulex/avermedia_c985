// SPDX-License-Identifier: GPL-2.0
/*
 * channel.c - Channel implementation from Ghidra decompile
 */

#include <linux/slab.h>
#include "types.h"
#include "structs.h"      /* Has struct QUEUE_ENTRY */
#include "queue.h"        /* Has struct c_queue */
#include "channel.h"
#include "cobject.h"
#include "ctask/ctask.h"
#include "qperrors.h"
#include "pins.h"
#include "include/abi/qp_buffer_descriptor.h"
#include "include/abi/_qp_ksstream_header.h"
#include "include/abi/cqueue.h"

/* Get c985_poc from embedded cql_codec */
#define codec_to_poc(c) container_of(c, struct c985_poc, codec)

_EQPErrors CChannel_DeviceCallback(struct c_channel *channel,
                                   u32 param,
                                   void *data)
{
    /* Validate channel state and context */
    if (!channel || !channel->m_bOpened || !channel->m_callbackContext)
        return QPERR_FAIL;

    /* Directly call the known callback implementation */
    return CDataPin_StreamCallback((struct c_data_pin *)channel->m_callbackContext,
                                   param,
                                   data);
}


int CYUVInChannel_GetBuffer(struct c_channel *channel,
                            struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                            u8 **buffer,
                            u32 *size)
{
    struct c985_poc *poc;
    struct QUEUE_ENTRY *entry;
    struct _QP_BUFFER_DESCRIPTOR *desc;
    struct _QP_KSSTREAM_HEADER
 *header;
    struct c_yuv_in_channel *yuv_channel;

    if (!channel || !buf_desc || !buffer || !size)
        return 0;

    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    yuv_channel = container_of(channel, struct c_yuv_in_channel, m_Channel);

    /* Get entry from request queue */
    entry = CQueue_GetOneEntry(channel->m_pDataRequestQueue);
    if (!entry) {
        pr_debug("CYUVInChannel_GetBuffer() no entries in queue\n");
        return 0;
    }

    desc = entry->Data;
    header = desc->DataBufferArray;

    /* Clear flags (keep upper 16 bits) */
    desc->ulFlags = desc->ulFlags & 0xffff0000;

    /* Check for ENDOFSTREAM or zero size */
    if ((header->OptionsFlags & 0x200) || (desc->ulBufferSize == 0)) {
        pr_debug("CYUVInChannel_GetBuffer() QP_KSSTREAM_HEADER_OPTIONSF_ENDOFSTREAM\n");
        desc->ulFlags |= 0x20000;
    }

    /* Check buffer size against frame size */
    if (desc->ulBufferSize < yuv_channel->m_dwFrameSize) {
        pr_debug("CYUVInChannel_GetBuffer() buffer size(%d) < frame size(%d)\n",
                 desc->ulBufferSize, yuv_channel->m_dwFrameSize);

        entry->Data = NULL;
        CQueue_AddEntry(channel->m_pFreeQueue, entry);

        /* Ensure Status is cleared before callback */
        desc->Status = 0;

        /* CChannel_DeviceCallback is a wrapper that passes m_callbackContext as 'this' */
        CChannel_DeviceCallback(channel, 0x10000, desc);

        return 0;
    }
    else {
        /* Success Path */

        /* Check PTS Flag (0x40000) */
        if ((desc->ulFlags & 0x40000) != 0) {
            pr_debug("CYUVInChannel_GetBuffer() PTS(%u) KSTime(%lu:%lu) Numerator(%d) Denominator(%d)\n");
        }

        /* Set buffer info */
        desc->ulBufferIndex = 0;
        desc->ulBufferOffset = 0;
        desc->ulTotalUsed = 0;

        *buffer = header->Data;
        *size = desc->ulBufferSize;
        *buf_desc = desc;

        pr_debug("CYUVInChannel_GetBuffer() buffer(0x%x) size(%d) Desc(0x%x) frame(%d)\n",
                 *buffer, *size, *buf_desc, yuv_channel->m_dwFrameSize);

        /* Move to pending queue */
        CQueue_AddEntry(channel->m_pDataPendingQueue, entry);

        return 1;
    }
}
int CYUVInChannel_GetBufferYUV(struct c_channel *channel,
                               struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                               u8 **y_buffer,
                               u32 *y_size,
                               u8 **uv_buffer,
                               u32 *uv_size)
{
    struct c985_poc *poc;
    struct QUEUE_ENTRY *entry;
    struct _QP_BUFFER_DESCRIPTOR *desc;
    struct _QP_KSSTREAM_HEADER
 *header;
    struct c_yuv_in_channel *yuv_channel;
    u32 frame_size;

    if (!channel || !buf_desc || !y_buffer || !y_size || !uv_buffer || !uv_size)
        return 0;

    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    yuv_channel = container_of(channel, struct c_yuv_in_channel, m_Channel);

    /* Get entry from request queue */
    entry = CQueue_GetOneEntry(channel->m_pDataRequestQueue);
    if (!entry) {
        dev_dbg(&poc->pdev->dev, "CYUVInChannel_GetBufferYUV: no entries in queue\n");
        return 0;
    }

    desc = entry->Data;
    header = desc->DataBufferArray;

    /* Clear flags (keep upper 16 bits) */
    desc->ulFlags = desc->ulFlags & 0xffff0000;

    /* Check for ENDOFSTREAM or zero size */
    if ((header->OptionsFlags & 0x200) || (desc->ulBufferSize == 0)) {
        dev_dbg(&poc->pdev->dev, "CYUVInChannel_GetBufferYUV: ENDOFSTREAM\n");
        desc->ulFlags |= 0x20000;
    }

    /* Check buffer size against frame size */
    if (desc->ulBufferSize < yuv_channel->m_dwFrameSize) {
        dev_err(&poc->pdev->dev,
                "CYUVInChannel_GetBufferYUV: buffer size(%u) < frame size(%u)\n",
                desc->ulBufferSize, yuv_channel->m_dwFrameSize);
        entry->Data = NULL;
        CQueue_AddEntry(channel->m_pFreeQueue, entry);
        desc->Status = 0;
        CChannel_DeviceCallback(channel, 0x10000, desc);
        return 0;
    }

    /* Calculate Y and UV sizes (NV12: Y = width*height, UV = Y/2) */
    frame_size = yuv_channel->m_lWidth * yuv_channel->m_lHeight;

    /* Set buffer info */
    desc->ulBufferIndex = 0;
    desc->ulBufferOffset = 0;
    desc->ulTotalUsed = 0;

    *y_buffer = header->Data;
    *y_size = frame_size;
    *uv_buffer = header->Data + frame_size;  /* UV follows Y */
    *uv_size = frame_size >> 1;              /* UV is half size (NV12) */
    *buf_desc = desc;

    dev_dbg(&poc->pdev->dev,
            "CYUVInChannel_GetBufferYUV: Y=%p(%u) UV=%p(%u) frame=%u\n",
            *y_buffer, *y_size, *uv_buffer, *uv_size, frame_size);

    /* Move to pending queue */
    CQueue_AddEntry(channel->m_pDataPendingQueue, entry);

    return 1;
}


/* ================================================================
 * STUBS - To be implemented later from Ghidra
 * ================================================================ */

/* Stubs for CYUVInChannel - to be implemented later */
int CYUVInChannel_Start(struct c_channel *channel)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_warn(&poc->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    channel->m_State = QPSTATE_RUN;
    return 0;
}

int CYUVInChannel_GetBufferRas(struct c_channel *channel,
                               struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                               u8 **y_buffer,
                               u32 *y_size,
                               u8 **u_buffer,
                               u32 *u_size,
                               u8 **v_buffer,
                               u32 *v_size)
{
    struct c985_poc *poc;
    struct CObject *parent;
    struct QUEUE_ENTRY *entry;
    struct _QP_BUFFER_DESCRIPTOR *desc;
    struct _QP_KSSTREAM_HEADER *header;
    struct c_yuv_in_channel *yuv_channel;
    u32 frame_size;
    int can_swap;

    if (!channel || !buf_desc || !y_buffer || !y_size || !u_buffer || !u_size || !v_buffer || !v_size)
        return 0;

    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    yuv_channel = container_of(channel, struct c_yuv_in_channel, m_Channel);
    parent = channel->m_Object.m_pParent;

    /* Get entry from request queue */
    entry = CQueue_GetOneEntry(channel->m_pDataRequestQueue);
    if (!entry) {
        dev_dbg(&poc->pdev->dev, "CYUVInChannel_GetBufferRas: no entries in queue\n");
        return 0;
    }

    dev_info(&poc->pdev->dev,
             "CYUVInChannel_GetBufferRas: entry=%px Data=%px\n",
             entry, entry->Data);


    desc = entry->Data;
    header = desc->DataBufferArray;

    /* Clear flags (keep upper 16 bits) */
    desc->ulFlags = desc->ulFlags & 0xffff0000;

    /* Check for ENDOFSTREAM or zero size */
    if ((header->OptionsFlags & 0x200) || (desc->ulBufferSize == 0)) {
        dev_dbg(&poc->pdev->dev, "CYUVInChannel_GetBufferRas: ENDOFSTREAM\n");
        desc->ulFlags |= 0x20000;
    }

    /* Check buffer size against frame size */
    if (desc->ulBufferSize < yuv_channel->m_dwFrameSize) {
        dev_err(&poc->pdev->dev,
                "CYUVInChannel_GetBufferRas: buffer size(%u) < frame size(%u)\n",
                desc->ulBufferSize, yuv_channel->m_dwFrameSize);
        entry->Data = NULL;
        CQueue_AddEntry(channel->m_pFreeQueue, entry);
        desc->Status = 0;
        CChannel_DeviceCallback(channel, 0x10000, desc);
        return 0;
    }

    /* Byte swap if needed (skip for now - implement QPHCI_CanSwapData later) */
    can_swap = 0;  /* TODO: Implement QPHCI_CanSwapData */
    if (channel->m_bByteSwap && !can_swap && desc->ulBufferSize != 0) {
        /* BytesSwap(header->Data, header->Data, desc->ulBufferSize); */
        /* TODO: Implement BytesSwap */
    }

    /* Set buffer info */
    desc->ulBufferIndex = 0;
    desc->ulBufferOffset = 0;
    desc->ulTotalUsed = 0;

    /* Calculate Y plane size (width * height) */
    frame_size = yuv_channel->m_lWidth * yuv_channel->m_lHeight;

    /* Set Y buffer */
    *y_buffer = header->Data;
    *y_size = frame_size;

    /* U and V sizes are each 1/4 of Y (planar YUV) */
    *u_size = frame_size >> 2;
    *v_size = frame_size >> 2;

    /* U/V order depends on nDataType field */
    if (yuv_channel->m_nDataType == 1) {
        /* YV12 order: Y, V, U */
        *v_buffer = header->Data + frame_size;
        *u_buffer = *v_buffer + *v_size;
    } else {
        /* I420 order: Y, U, V */
        *u_buffer = header->Data + frame_size;
        *v_buffer = *u_buffer + *u_size;
    }

    *buf_desc = desc;

    dev_dbg(&poc->pdev->dev,
            "CYUVInChannel_GetBufferRas: Y=%p(%u) U=%p(%u) V=%p(%u) frame=%d\n",
            *y_buffer, *y_size, *u_buffer, *u_size, *v_buffer, *v_size, frame_size);

    /* Move to pending queue */
    CQueue_AddEntry(channel->m_pDataPendingQueue, entry);

    return 1;
}

/*
 * CYUVInChannel_CompleteBuffer
 *
 * Called when the ARM firmware has finished writing to a YUV buffer.
 * Moves the buffer from Pending queue back to Free queue.
 */
int CYUVInChannel_CompleteBuffer(struct c_channel *channel,
                                 struct _QP_BUFFER_DESCRIPTOR *buf_desc)
{
    struct c985_poc *poc;
    struct c_yuv_in_channel *yuv_channel;
    struct QUEUE_ENTRY *entry;

    /* NULL checks FIRST, before accessing anything */
    if (!channel || !buf_desc)
        return 0;

    /* NOW it's safe to get poc */
    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);

    dev_info(&poc->pdev->dev, "CYUVInChannel_CompleteBuffer ENTER (buf_desc=%px)\n", buf_desc);

    /*
     * The decompiled code uses pointer arithmetic (&param_1[1]...) to access
     * the parent struct. In Linux, we use container_of to safely access
     * c_yuv_in_channel from the embedded c_channel member.
     */
    yuv_channel = container_of(channel, struct c_yuv_in_channel, m_Channel);

    /* Find the entry in the Pending queue associated with this buffer descriptor */
    entry = CQueue_GetEntryByData(channel->m_pDataPendingQueue, buf_desc);
    if (!entry) {
        dev_warn(&poc->pdev->dev,
                 "CYUVInChannel_CompleteBuffer: Entry not found for desc=%p\n",
                 buf_desc);
        return 0;
    }

    /* Check Status */
    if (buf_desc->Status < 0) {
        dev_err(&poc->pdev->dev,
                "CYUVInChannel_CompleteBuffer() Status(%d)\n",
                buf_desc->Status);
    } else {
        /* Increment frame counter (matches decompiled pointer arithmetic logic) */
        yuv_channel->m_ulVideoFramesCount++;
        dev_dbg(&poc->pdev->dev,
                "CYUVInChannel_CompleteBuffer: Frame %u completed\n",
                yuv_channel->m_ulVideoFramesCount);
    }

    /* Clear entry data */
    entry->Data = NULL;

    /* Return entry to Free queue */
    CQueue_AddEntry(channel->m_pFreeQueue, entry);

    /* Call device callback (Event 0x10000 = Buffer Complete) */
    if (channel->m_pDeviceCallback) {
        channel->m_pDeviceCallback(channel, 0x10000, buf_desc, channel->m_callbackContext);
    }

    return 1;
}

int CYUVInChannel_GetResolution(struct c_channel *channel, u32 *width, u32 *height)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_warn(&poc->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    if (width) *width = 1920;
    if (height) *height = 1080;
    return 0;
}

int CYUVInChannel_GetYUVFormat(struct c_channel *channel, u32 *format)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_warn(&poc->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    if (format) format[0] = 0;  /* Planar */
        return 0;
}


int CChannel_AddBuffer(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    param_1->m_State = QPSTATE_RUN;
    return 0;
}

int CChannel_Start(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    param_1->m_State = QPSTATE_RUN;
    return 0;
}

int CChannel_Stop(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    param_1->m_State = QPSTATE_STOP;
    return 0;
}

int CChannel_Acquire(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_Pause(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    if (param_1->m_State == QPSTATE_RUN)
        param_1->m_State = QPSTATE_PAUSE;
    return 0;
}

int CChannel_SetRate(struct c_channel *param_1, void *param_2)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_GetRate(struct c_channel *param_1, void *param_2)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_BeginFlush(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    param_1->m_bFlushing = 1;
    return 0;
}

int CChannel_RecycleEntry(struct c_channel *channel, struct QUEUE_ENTRY *entry, int cancel)
{
    struct _QP_BUFFER_DESCRIPTOR *desc;
    void *data;
    int ret;

    if (!entry)
        return 1;

    /* Optional cancellation step */
    if (cancel) {
        if (channel->m_pTask && channel->m_pTask->CancelBuffer) {
            ret = ((int (*)(struct c_task *, u32, int, void *))
            channel->m_pTask->CancelBuffer)(
                channel->m_pTask,
                channel->m_hTask,
                channel->m_dataType,
                entry->Data);

            /* CancelBuffer returned 0 = failure/stop, do not recycle */
            if (ret == 0)
                return 0;
        }
    }

    /* Save data pointer before clearing entry */
    data = entry->Data;
    entry->Data = NULL;

    /* Clear status in buffer descriptor */
    if (data) {
        desc = (struct _QP_BUFFER_DESCRIPTOR *)data;
        /*
         * Offset 0x34 in qp_buffer_descriptor is Status (u8) followed by _pad1[3].
         * Decomp writes undefined4 (4 bytes), so we clear Status and padding.
         * This is safe due to padding alignment.
         */
        *(u32 *)&desc->Status = 0;
    }

    /* Return entry to free queue */
    CQueue_AddEntry(channel->m_pFreeQueue, entry);

    /* Notify via device callback */
    CChannel_DeviceCallback(channel, 0x10000, data);

    return 1;
}

int CChannel_Flush(struct c_channel *channel)
{
    struct c_task *task;
    struct cql_codec *codec;
    struct c985_poc *poc;
    struct QUEUE_ENTRY *entry;
    int ret;

    if (!channel)
        return QPERR_INVALID;

    task = channel->m_pTask;
    if (!task)
        return QPERR_INVALID;

    /* Get device context for debug logging */
    codec = task->m_pMpegCodec;
    if (codec) {
        poc = container_of(codec, struct c985_poc, codec);
        dev_dbg(&poc->pdev->dev, "CChannel_Flush()[\n");
    }

    /* Call task flush callback */
    if (task->Flush) {
        ((void (*)(struct c_task *, u32, int))task->Flush)(
            task, channel->m_hTask, channel->m_dataType);
    }

    /* Drain pending queue */
    while (1) {
        entry = CQueue_GetOneEntry(channel->m_pDataPendingQueue);
        if (!entry) {
            /* Pending queue empty - drain request queue */
            while ((entry = CQueue_GetOneEntry(channel->m_pDataRequestQueue)) != NULL) {
                CChannel_RecycleEntry(channel, entry, 0);
            }
            return QPERR_SUCCESS;
        }

        ret = CChannel_RecycleEntry(channel, entry, 1);
        if (ret == 0) {
            /* Recycle failed or could not proceed - restore entry and return error */
            CQueue_AddEntry(channel->m_pDataPendingQueue, entry);
            return QPERR_INVALID;
        }
        /* Recycle succeeded (non-zero), continue loop */
    }
}

int CChannel_EndFlush(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    param_1->m_bFlushing = 0;
    return 0;
}

int CChannel_CancelBuffer(struct c_channel *param_1, void *param_2)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_TimeoutBuffer(struct c_channel *param_1, void *param_2)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_EndChannelChange(struct c_channel *param_1)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    return 0;
}

int CChannel_GetResolution(struct c_channel *param_1, u32 *width, u32 *height)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    if (width) *width = 1920;
    if (height) *height = 1080;
    return 0;
}

int CChannel_GetYUVFormat(struct c_channel *param_1, u32 *format)
{
    struct c985_poc *d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);
    dev_warn(&d->pdev->dev, "STUB: %s() - NEEDS IMPLEMENTING\n", __func__);
    if (format) format[0] = 0;  /* Planar */
        return 0;
}


/* ================================================================
 * CChannel_Constructor - from Ghidra
 * ================================================================ */
struct c_channel *CChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                       u32 param_3, u32 param_4, int param_5, int param_6,
                                       enum channel_direction param_7, u32 param_8,
                                       struct c_task *param_9)
{
    struct c985_poc *d;
    struct c_queue *pCVar1;
    struct c_queue *pCVar2;

    if (param_1 == NULL)
        return NULL;

    d = codec_to_poc(param_9->m_pMpegCodec);

    CObject_Constructor(&param_1->m_Object, param_2, param_3);

    /* Set function pointers */
    param_1->Open = CChannel_Open;
    param_1->Close = CChannel_Close;
    param_1->Start = CChannel_Start;
    param_1->Stop = CChannel_Stop;
    param_1->Acquire = CChannel_Acquire;
    param_1->Pause = CChannel_Pause;
    param_1->Step = NULL;
    param_1->SetRate = CChannel_SetRate;
    param_1->GetRate = CChannel_GetRate;
    param_1->BeginFlush = CChannel_BeginFlush;
    param_1->Flush = CChannel_Flush;
    param_1->EndFlush = CChannel_EndFlush;
    param_1->AddBuffer = CChannel_AddBuffer;
    param_1->CancelBuffer = CChannel_CancelBuffer;
    param_1->TimeoutBuffer = CChannel_TimeoutBuffer;
    param_1->XferData = NULL;
    param_1->GetBuffer = NULL;
    param_1->GetBufferYUVRAS = NULL;
    param_1->CompleteBuffer = NULL;
    param_1->BeginChannelChange = CChannel_EndChannelChange;
    param_1->EndChannelChange = CChannel_EndChannelChange;
    param_1->GetResolution = CChannel_GetResolution;
    param_1->GetYUVFormat = CChannel_GetYUVFormat;

    /* Initialize member variables */
    param_1->m_dwOpenFlags = 0;
    param_1->m_pDeviceCallback = NULL;
    param_1->m_callbackContext = NULL;
    param_1->m_hTask = param_4;
    param_1->m_hChannel = 0xFFFFFFFF;
    param_1->m_dataType = param_5;
    param_1->m_FWDataType = param_6;
    param_1->m_ChannelDirection = param_7;
    param_1->m_ChannelType = param_8;
    param_1->m_pTask = param_9;
    param_1->m_bOpened = 0;
    param_1->m_State = QPSTATE_STOP;
    param_1->m_bPaused = 0;
    param_1->m_bFlushing = 0;
    param_1->m_bByteSwap = 0;
    param_1->m_ullCntBytes = 0;
    param_1->m_llStartTime = 0;

    /* Initialize queues to NULL first */
    param_1->m_pFreeQueue = NULL;
    param_1->m_pDataRequestQueue = NULL;
    param_1->m_pDataPendingQueue = NULL;

    /* Create queues (only if dataType != 6) */
    if (param_5 != 6) {
        /* Create m_pFreeQueue */
        pCVar1 = kzalloc(sizeof(struct c_queue), GFP_KERNEL);
        if (pCVar1 == NULL) {
            CChannel_Destructor(param_1);
            return NULL;
        }
        pCVar2 = CQueue_Constructor(pCVar1, &param_1->m_Object, 2);
        param_1->m_pFreeQueue = pCVar2;
        if (param_1->m_pFreeQueue == NULL) {
            kfree(pCVar1);
            CChannel_Destructor(param_1);
            return NULL;
        }

        /* Create m_pDataRequestQueue */
        pCVar1 = kzalloc(sizeof(struct c_queue), GFP_KERNEL);
        if (pCVar1 == NULL) {
            CChannel_Destructor(param_1);
            return NULL;
        }
        pCVar2 = CQueue_Constructor(pCVar1, &param_1->m_Object, 2);
        param_1->m_pDataRequestQueue = pCVar2;
        if (param_1->m_pDataRequestQueue == NULL) {
            kfree(pCVar1);
            CChannel_Destructor(param_1);
            return NULL;
        }

        /* Create m_pDataPendingQueue */
        pCVar1 = kzalloc(sizeof(struct c_queue), GFP_KERNEL);
        if (pCVar1 == NULL) {
            CChannel_Destructor(param_1);
            return NULL;
        }
        pCVar2 = CQueue_Constructor(pCVar1, &param_1->m_Object, 2);
        param_1->m_pDataPendingQueue = pCVar2;
        if (param_1->m_pDataPendingQueue == NULL) {
            kfree(pCVar1);
            CChannel_Destructor(param_1);
            return NULL;
        }

        dev_dbg(&d->pdev->dev,
                "CChannel_Constructor() queues created: Free=%p Request=%p Pending=%p\n",
                param_1->m_pFreeQueue, param_1->m_pDataRequestQueue, param_1->m_pDataPendingQueue);
    }

    dev_dbg(&d->pdev->dev,
            "CChannel_Constructor() hTask(%u) dataType(%d) fwDataType(0x%x) dir(%d)\n",
            param_4, param_5, param_6, param_7);

    return param_1;
}
/* ================================================================
 * CChannel_Open - from Ghidra
 * ================================================================ */
int CChannel_Open(struct c_channel *param_1, u32 param_2, u32 param_3, void *param_4,
                  void *param_5, void *param_6)
{
    struct c985_poc *d;
    int ret;
    int i;

    if (param_1 == NULL)
        return QPERR_PARMS;

    d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);

    dev_dbg(&d->pdev->dev, "CChannel_Open()\n");

    param_1->m_hChannel = param_2;
    param_1->m_dwOpenFlags = param_3;
    param_1->m_pDeviceCallback = param_5;
    param_1->m_callbackContext = param_6;

    for (i = 0; i < 256; i++) {
        memset(&param_1->m_Entries[i], 0, sizeof(struct QUEUE_ENTRY));
    }

    param_1->m_State = 0;
    param_1->m_bPaused = 0;
    param_1->m_bFlushing = 0;
    param_1->m_ullCntBytes = 0;
    param_1->m_savedNumberBytes = 0;
    param_1->m_llBasePts = 0;
    param_1->m_llPrevPts = 0;
    param_1->m_llAddPts = 0;
    param_1->m_bOpened = 1;

    ret = CTask_Open(param_1->m_pTask, param_1->m_hTask,
                     (enum task_data_type)param_1->m_dataType,
                     param_1->m_ChannelDirection,
                     param_1->m_FWDataType, param_1);

    if (ret < 0) {
        dev_dbg(&d->pdev->dev, "CChannel_Open() CTask_Open failed (%d)\n", ret);
        if (param_1->Close)
            ((int (*)(struct c_channel *))param_1->Close)(param_1);
    }

    return ret;
}

/* ================================================================
 * CChannel_Close - from Ghidra
 * ================================================================ */
int CChannel_Close(struct c_channel *param_1)
{
    struct c985_poc *d;

    if (param_1 == NULL)
        return QPERR_PARMS;

    d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);

    dev_dbg(&d->pdev->dev, "CChannel_Close()\n");

    if (param_1->m_bOpened != 0) {
        if (param_1->m_State == 1) {
            if (param_1->Pause)
                ((int (*)(struct c_channel *))param_1->Pause)(param_1);
        }

        if (param_1->m_State != 0) {
            dev_dbg(&d->pdev->dev,
                    "CChannel_Close() !!! Not Stopped State (%d)!!!!\n",
                    param_1->m_State);
            if (param_1->Stop)
                ((int (*)(struct c_channel *))param_1->Stop)(param_1);
        }

        if (param_1->Flush)
            ((int (*)(struct c_channel *))param_1->Flush)(param_1);

        if (param_1->m_pTask && param_1->m_pTask->Close) {
            ((int (*)(struct c_task *, u32, int, int))param_1->m_pTask->Close)(
                param_1->m_pTask, param_1->m_hTask, param_1->m_dataType, 1);
        }

        param_1->m_bOpened = 0;
    }

    return QPERR_SUCCESS;
}


void CChannel_Destructor(struct c_channel *param_1)
{
    if (param_1->m_bOpened != 0) {
        ((int (*)(struct c_channel *))param_1->Close)(param_1);
    }

    if (param_1->m_pDataPendingQueue != NULL) {
        kfree(param_1->m_pDataPendingQueue);
        param_1->m_pDataPendingQueue = NULL;
    }

    if (param_1->m_pDataRequestQueue != NULL) {
        kfree(param_1->m_pDataRequestQueue);
        param_1->m_pDataRequestQueue = NULL;
    }

    if (param_1->m_pFreeQueue != NULL) {
        kfree(param_1->m_pFreeQueue);
        param_1->m_pFreeQueue = NULL;
    }

    CObject_Destructor(&param_1->m_Object);
}


/* ================================================================
 * CAESOutChannel_Constructor - from Ghidra
 * ================================================================ */
struct c_channel *CAESOutChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                             u32 param_3, u32 param_4, struct c_task *param_5)
{
    struct c_channel *pCVar1;
    struct c985_poc *d;

    if (param_1 == NULL)
        return NULL;

    d = codec_to_poc(param_5->m_pMpegCodec);

    pCVar1 = CChannel_Constructor(param_1, param_2, param_3, param_4,
                                  1, 0x84, CHANNEL_DIR_READ,
                                  QPMPGCODEC_ENC_AES_OUT, param_5);

    if (pCVar1 == NULL)
        return NULL;

    param_1->m_bByteSwap = 1;

    dev_dbg(&d->pdev->dev, "CAESOutChannel_Constructor()\n");

    return param_1;
}

/* ================================================================
 * CYUVInChannel_Constructor - from Ghidra
 * ================================================================ */
struct c_channel *CYUVInChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                            u32 param_3, u32 param_4, struct c_task *param_5)
{
    struct c_channel *pCVar1;
    struct c985_poc *d;
    struct c_yuv_in_channel *yuv_channel;

    if (param_1 == NULL)
        return NULL;

    d = codec_to_poc(param_5->m_pMpegCodec);

    pCVar1 = CChannel_Constructor(param_1, param_2, param_3, param_4,
                                  4, 1, CHANNEL_DIR_WRITE,
                                  QPMPGCODEC_ENC_YUV_IN, param_5);
    if (pCVar1 == NULL)
        return NULL;

    yuv_channel = container_of(param_1, struct c_yuv_in_channel, m_Channel);

    param_1->m_bByteSwap = 1;
    param_1->Open = CYUVInChannel_Open;

    /* Set function pointers (from Windows decompile) */
    param_1->Start = CYUVInChannel_Start;
    param_1->Stop = CMPEGOutChannel_Stop;
    param_1->Pause = CMPEGOutChannel_Pause;
    param_1->GetBuffer = CYUVInChannel_GetBuffer;
    param_1->GetBufferYUV = CYUVInChannel_GetBufferYUV;
    param_1->GetBufferYUVRAS = CYUVInChannel_GetBufferRas;
    param_1->CompleteBuffer = CYUVInChannel_CompleteBuffer;
    param_1->GetResolution = CYUVInChannel_GetResolution;
    param_1->GetYUVFormat = CYUVInChannel_GetYUVFormat;

    /* Initialize YUV-specific fields */
    yuv_channel->m_dwFrameSize = 0;
    yuv_channel->m_ulVideoFramesCount = 0;
    yuv_channel->m_lWidth = 0;
    yuv_channel->m_lHeight = 0;
    yuv_channel->m_nBitCount = 0;
    yuv_channel->m_nDataType = 0;

    dev_dbg(&d->pdev->dev, "CYUVInChannel_Constructor()\n");

    return param_1;
}

/* ================================================================
 * CYUVInChannel_Open - from Ghidra
 * ================================================================ */
int CYUVInChannel_Open(struct c_channel *param_1, u32 param_2, u32 param_3,
                       void *param_4, void *param_5, void *param_6)
{
    struct c985_poc *d;
    int iVar2;
    int local_10;
    u32 *fmt = (u32 *)param_4;
    u32 width, height, bit_count, data_type;
    u32 frame_size;

    d = codec_to_poc(param_1->m_pTask->m_pMpegCodec);

    if (fmt) {
        width = fmt[0];
        height = fmt[1];
        bit_count = fmt[2];
        data_type = fmt[4];

        iVar2 = width * height * bit_count;
        frame_size = (iVar2 + (iVar2 >> 31 & 7)) >> 3;

        if ((bit_count == 0xc) && ((data_type == 1) || (data_type == 2))) {
            local_10 = 1;
        } else {
            local_10 = 0;
        }
        param_1->m_bByteSwap = local_10;

        if (bit_count == 0x10) {
            param_1->m_FWDataType = 7;
        } else {
            param_1->m_FWDataType = 1;
        }

        dev_dbg(&d->pdev->dev,
                "CYUVInChannel_Open() m_dwFrameSize(%d) m_lWidth(%d) m_lHeight(%d) bitCnt(%d) swap(%d) m_nDataType(%d)\n",
                frame_size, width, height, bit_count, param_1->m_bByteSwap, data_type);
    } else {
        param_1->m_FWDataType = 1;
        dev_dbg(&d->pdev->dev, "CYUVInChannel_Open() no format info, using fw_data_type=1\n");
    }

    return CChannel_Open(param_1, param_2, param_3, param_4, param_5, param_6);
}

/* ================================================================
 * CPCMInChannel_Constructor - from Ghidra
 * ================================================================ */
struct c_channel *CPCMInChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                            u32 param_3, u32 param_4, struct c_task *param_5)
{
    struct c_channel *pCVar1;
    struct c985_poc *d;

    if (param_1 == NULL)
        return NULL;

    d = codec_to_poc(param_5->m_pMpegCodec);

    pCVar1 = CChannel_Constructor(param_1, param_2, param_3, param_4,
                                  5, 2, CHANNEL_DIR_WRITE,
                                  QPMPGCODEC_ENC_PCM_IN, param_5);
    if (pCVar1 == NULL)
        return NULL;

    dev_dbg(&d->pdev->dev, "CPCMInChannel_Constructor()\n");

    return param_1;
}


/* Linux Native */

int CMPEGOutChannel_GetBuffer(struct c_channel *channel,
                              struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                              u8 **buffer,
                              u32 *size)
{
    struct c985_poc *poc;
    struct c985_buffer *vbuf;
    struct vb2_buffer *vb;
    unsigned long flags;

    if (!channel || !channel->m_pTask)
        return 0;

    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);

    /* Grab a buffer from V4L2 queue */
    spin_lock_irqsave(&poc->buf_lock, flags);

    if (list_empty(&poc->buf_list)) {
        spin_unlock_irqrestore(&poc->buf_lock, flags);
        dev_dbg(&poc->pdev->dev, "CMPEGOutChannel_GetBuffer: no buffers available\n");
        return 0;
    }

    vbuf = list_first_entry(&poc->buf_list, struct c985_buffer, list);
    list_del(&vbuf->list);

    spin_unlock_irqrestore(&poc->buf_lock, flags);

    vb = &vbuf->vb.vb2_buf;

    /* Store the vbuf pointer in channel for later completion */
    // channel->m_pending_vbuf = vbuf;

    /* Return buffer info - use vb2 plane address */
    *buffer = vb2_plane_vaddr(vb, 0);
    *size = vb2_plane_size(vb, 0);
    *buf_desc = NULL;  /* We don't use Windows descriptor, use m_pending_vbuf instead */

    dev_dbg(&poc->pdev->dev,
            "CMPEGOutChannel_GetBuffer: got buffer %p size %u\n",
            *buffer, *size);

    return 1;
}
/* Linux Native */
int CMPEGOutChannel_CompleteBuffer(struct c_channel *channel, u32 bytes_used)
{
    struct c985_poc *poc;
    struct c985_buffer *vbuf;
    struct vb2_buffer *vb;

  //  if (!channel || !channel->m_pTask || !channel->m_pending_vbuf)
  //      return 0;

    poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
  //  vbuf = (struct c985_buffer *)channel->m_pending_vbuf;
    vb = &vbuf->vb.vb2_buf;

    /* Set payload size */
    vb2_set_plane_payload(vb, 0, bytes_used);

    /* Set timestamp and sequence */
    vbuf->vb.vb2_buf.timestamp = ktime_get_ns();
    vbuf->vb.sequence = poc->sequence++;
    vbuf->vb.field = V4L2_FIELD_NONE;

    /* Return buffer to userspace */
    vb2_buffer_done(vb, VB2_BUF_STATE_DONE);

 //   channel->m_pending_vbuf = NULL;

    dev_dbg(&poc->pdev->dev,
            "CMPEGOutChannel_CompleteBuffer: completed %u bytes seq %u\n",
            bytes_used, poc->sequence - 1);

    return 1;
}
/* Linux Native */
struct c_channel *CMPEGOutChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                              u32 param_3, u32 param_4, struct c_task *param_5)
{
    struct c_channel *pCVar1;
    struct c985_poc *poc;

    if (param_1 == NULL)
        return NULL;

    poc = codec_to_poc(param_5->m_pMpegCodec);

    pCVar1 = CChannel_Constructor(param_1, param_2, param_3, param_4,
                                  0, 0x83, CHANNEL_DIR_READ,
                                  QPMPGCODEC_ENC_MPEG_OUT, param_5);
    if (pCVar1 == NULL)
        return NULL;

    /* Set byte swap based on input control bit 6 */
    if ((param_5->m_TaskData[param_4].m_inputControl >> 6 & 1) == 0) {
        param_1->m_bByteSwap = 1;
    } else {
        param_1->m_bByteSwap = 0;
    }

    /* Set function pointers */
    param_1->Start = CMPEGOutChannel_Start;
    param_1->Stop = CMPEGOutChannel_Stop;
    param_1->Pause = CMPEGOutChannel_Pause;
    param_1->GetBuffer = CMPEGOutChannel_GetBuffer;
    param_1->CompleteBuffer = CMPEGOutChannel_CompleteBuffer;

 //   param_1->m_pending_vbuf = NULL;

    dev_dbg(&poc->pdev->dev, "CMPEGOutChannel_Constructor()\n");

    return param_1;
}
int CMPEGOutChannel_Start(struct c_channel *channel)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CMPEGOutChannel_Start() - STUB\n");
    channel->m_State = QPSTATE_RUN;
    return 0;
}

int CMPEGOutChannel_Stop(struct c_channel *channel)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CMPEGOutChannel_Stop() - STUB\n");
    channel->m_State = QPSTATE_STOP;
    return 0;
}

int CMPEGOutChannel_Pause(struct c_channel *channel)
{
    struct c985_poc *poc = codec_to_poc(channel->m_pTask->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CMPEGOutChannel_Pause() - STUB\n");
    if (channel->m_State == QPSTATE_RUN) {
        channel->m_State = QPSTATE_PAUSE;
    }
    return 0;
}

int CChannel_needByteSwapping(struct c_channel *ch)
{
    if (!ch)
        return 0;

    return ch->m_bByteSwap;

}
/*
int CVBIOutChannel_GetVBIFormat(struct c_vbi_out_channel *chan, struct qp_vbi_dataformat *fmt)
{
    * Stub: return error to trigger default VBI info path *
    return -1;
}
*/

