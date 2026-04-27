// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_io.c - IO block building and sending
 */

#include "ctask_private.h"

/* ================================================================
 * CTask_isIOReady
 * ================================================================ */
int CTask_isIOReady(struct c_task *task, enum channel_direction dir)
{
    return (task->m_pPending[(int)dir] == NULL) ? 1 : 0;
}

/* ================================================================
 * CTask_BuildIoBlock
 * ================================================================ */
void CTask_BuildIoBlock(struct c_task *task, struct task_data *td,
                        enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    enum channel_direction dir;
    int ready;
    u32 available;
    u32 xfer_size;
    int idx = (int)data_type;

    dir = td->direction[idx];

    CObject_enterCritical(&task->m_CritSectionIOPending);

    ready = CTask_isIOReady(task, dir);
    if (ready == 0)
        goto out;

    /* Calculate available data */
    available = td->UserBuffer[idx].BUFFER.YUV.ulYLength -
    td->UserBuffer[idx].pBufDesc->ulBufferOffset;

    if (available > td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved3 * 4 -
        td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4) {
        available = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved3 * 4 -
        td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4;
        }

        if (available == 0) {
            dev_dbg(&poc->pdev->dev,
                    "CTask_BuildIoBlock() user buf size(%d) offset(%d) arm buf size(%d) offset(%d)\n",
                    td->UserBuffer[idx].BUFFER.YUV.ulYLength,
                    td->UserBuffer[idx].pBufDesc->ulBufferOffset,
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved3 * 4,
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4);

            if (td->type == TASK_TYPE_ENC)
                CEncoderTask_ProcessIoComplete(task, td, data_type, 0);
            else
                CDecoderTask_ProcessIoComplete(task, td, data_type, 0);
            goto out;
        }

        xfer_size = min(available, task->m_dwMaxDMASize);

        task->m_ioPending[(int)dir].pHostAddress =
        td->UserBuffer[idx].BUFFER.YUV.pYBuffer +
        td->UserBuffer[idx].pBufDesc->ulBufferOffset;

        task->m_ioPending[(int)dir].ulArmAddress =
        (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 >> 2) +
        td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.valid;

        task->m_ioPending[(int)dir].ulLength = min(xfer_size, task->m_dwMaxDMASize);
        task->m_ioPending[(int)dir].ulXferMode = 0;

        /* Set transfer mode flags */
        if ((td->UserBuffer[idx].pBufDesc->ulFlags & 0x10000000) == 0) {
            if ((td->UserBuffer[idx].pBufDesc->ulFlags & 0x20000000) == 0)
                task->m_ioPending[(int)dir].ulXferMode |= 0x2000000;
            else
                task->m_ioPending[(int)dir].ulXferMode |= 0x1000000;
        }

        task->m_ioPending[(int)dir].ulPicWidth = 0;
        task->m_ioPending[(int)dir].ulPicHeight = 0;

        if ((task->m_ioPending[(int)dir].ulLength & 3) != 0) {
            dev_warn(&poc->pdev->dev,
                     "CTask_BuildIoBlock() size(0x%x) not dword aligned\n",
                     task->m_ioPending[(int)dir].ulLength);
        }

        task->m_ioPending[(int)dir].pTaskData = td;
        task->m_ioPending[(int)dir].dataType = data_type;
        task->m_ioPending[(int)dir].status = 1;
        task->m_pPending[(int)dir] = &task->m_ioPending[(int)dir];

        out:
        CObject_leaveCritical(&task->m_CritSectionIOPending);
}

/* ================================================================
 * CTask_SendIoBlock
 * ================================================================ */
void CTask_SendIoBlock(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    enum channel_direction dir;
    int swap;

    dir = td->direction[(int)data_type];
    if (task->m_pPending[(int)dir] == NULL)
        return;

    swap = CChannel_needByteSwapping(td->pChannel[(int)data_type]);

    if ((data_type != TASK_DATA_TYPE_COMP_AUD || task->m_taskIdAESOut != td->id) &&
        (data_type != TASK_DATA_TYPE_PCM || task->m_taskIdPCMOut != td->id)) {
        td->m_StartID = ((int)data_type + TASK_DATA_TYPE_COMP_AUD) % 7;
    task->m_StartID = (td->id + 1) % 8;
    dev_dbg(&poc->pdev->dev,
            "CTask_SendIoBlock() (%d:%d) next(%d:%d)\n",
            td->id, (int)data_type, task->m_StartID, td->m_StartID);
        }

        if (dir == CHANNEL_DIR_READ) {
            if (task->m_pPending[0]->ulLength < 4) {
                dev_dbg(&poc->pdev->dev,
                        "CTask_SendIoBlock() CHANNEL_DIR_READ size(%d)\n",
                        task->m_pPending[0]->ulLength);
                QPOSMSetEvtgrp(task->m_Thread.m_EvtWait, THREAD_EVENT_DMA_READ);
            } else {
                CQLCodec_StartDMARead_Full(
                    task->m_pMpegCodec,
                    task->m_pPending[0]->ulArmAddress,
                    task->m_pPending[0]->pHostAddress,
                    task->m_pPending[0]->ulLength & 0xFFFFFFFC,
                    swap, 0,
                    task->m_pPending[0]->ulXferMode,
                    task->m_pPending[0]->ulPicWidth,
                    task->m_pPending[0]->ulPicHeight, 1);
            }
        } else if (dir == CHANNEL_DIR_WRITE) {
            if (task->m_pPending[1]->ulLength < 4) {
                dev_dbg(&poc->pdev->dev,
                        "CTask_SendIoBlock() CHANNEL_DIR_WRITE size(%d)\n",
                        task->m_pPending[1]->ulLength);
                QPOSMSetEvtgrp(task->m_Thread.m_EvtWait, THREAD_EVENT_DMA_WRITE);
            } else {
                CQLCodec_StartDMAWrite_Full(
                    task->m_pMpegCodec,
                    task->m_pPending[1]->ulArmAddress,
                    task->m_pPending[1]->pHostAddress,
                    task->m_pPending[1]->ulLength & 0xFFFFFFFC,
                    swap, 0,
                    task->m_pPending[1]->ulXferMode,
                    task->m_pPending[1]->ulPicWidth,
                    task->m_pPending[1]->ulPicHeight, 1);
            }
        } else {
            dev_dbg(&poc->pdev->dev,
                    "CTask_SendIoBlock() Direction (%d) How do we get here???\n", (int)dir);
            CObject_enterCritical(&task->m_CritSectionIOPending);
            task->m_pPending[(int)dir] = NULL;
            CObject_leaveCritical(&task->m_CritSectionIOPending);
        }
}

/* ================================================================
 * Stubs - TODO: implement from Ghidra
 * ================================================================ */
/* ================================================================
 * Stubs - TODO: implement from Ghidra
 * ================================================================ */

void CTask_BuildIoBlockYUV(struct c_task *task, struct task_data *td,
                           enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_BuildIoBlockYUV() dataType(%d) - NEEDS IMPLEMENTING\n",
            data_type);
}

void CTask_BuildIoBlockYUVMB2RAS(struct c_task *task, struct task_data *td,
                                 enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_BuildIoBlockYUVMB2RAS() dataType(%d) - NEEDS IMPLEMENTING\n",
            data_type);
}
void CTask_BuildIoBlockYUVRAS(struct c_task *task, struct task_data *task_data,
                              enum task_data_type data_type)
{
    struct c985_poc *poc;
    enum channel_direction dir;
    u32 y_remaining, uv_remaining, v_remaining;
    u32 y_offset, uv_offset, v_offset;
    u32 y_arm_var, uv_arm_var, v_arm_var;
    u32 y_arm_base, uv_arm_base, v_arm_base;
    u32 transfer_len;
    u32 xfer_mode;
    unsigned long flags;
    int io_ready;
    struct task_io_pending *io_pending;
    struct _QP_BUFFER_DESCRIPTOR *buf_desc;

    if (!task || !task_data)
        return;

    poc = codec_to_poc(task->m_pMpegCodec);
    dir = task_data->direction[data_type];

    /* Enter Critical Section (Linux: spin_lock) */
    spin_lock_irqsave(&task->m_CritSectionIOPending.m_spinlock, flags);

    io_ready = CTask_isIOReady(task, dir);
    if (io_ready != 0) {
        /* Calculate remaining lengths */
        y_remaining = task_data->UserBuffer[data_type].BUFFER.YUV.ulYLength -
        task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved4;

        uv_remaining = task_data->UserBuffer[data_type].BUFFER.YUV.ulUVLength -
        task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved5;

        v_remaining = task_data->UserBuffer[data_type].BUFFER.YUV.reserved2 -
        task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved6;

        /* Extract offsets and ARM address vars */
        y_offset = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved4;
        y_arm_var = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved4;
        y_arm_base = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.valid;

        uv_offset = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved5;
        uv_arm_var = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved5;
        uv_arm_base = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved1;

        v_offset = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved6;
        v_arm_var = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved6;
        v_arm_base = task_data->ArmRequest[data_type].ArmBuffer.BUFFER.ALL.reserved2;

        /* Get buffer descriptor for flags */
        buf_desc = task_data->UserBuffer[data_type].pBufDesc;

        /* Determine XferMode based on flags */
        xfer_mode = 0;
        if (buf_desc) {
            if ((buf_desc->ulFlags & 0x10000000) == 0) {
                if ((buf_desc->ulFlags & 0x20000000) == 0) {
                    xfer_mode |= 0x2000000;
                } else {
                    xfer_mode |= 0x1000000;
                }
            }
        }

        /* Get pending IO slot */
        io_pending = &task->m_ioPending[dir];

        if (y_remaining == 0) {
            if (uv_remaining == 0) {
                if (v_remaining == 0) {
                    dev_dbg(&poc->pdev->dev,
                            "CTask_BuildIoBlockYUVRAS() How do we get here???\n");

                    if (task_data->type == TASK_TYPE_ENC) {
                        CEncoderTask_ProcessIoComplete(task, task_data, data_type, 0);
                    } else {
                        CDecoderTask_ProcessIoComplete(task, task_data, data_type, 0);
                    }
                } else {
                    /* V Plane */
                    if (task->m_dwMaxDMASize < v_remaining)
                        v_remaining = task->m_dwMaxDMASize;

                    io_pending->pHostAddress = (void *)(task_data->UserBuffer[data_type]
                    .BUFFER.YUV.reserved1 + v_offset);
                    io_pending->ulArmAddress = (v_arm_var >> 2) + v_arm_base;
                    io_pending->ulLength = v_remaining;
                    io_pending->ulXferMode = xfer_mode;
                    io_pending->ulPicWidth = 0;
                    io_pending->ulPicHeight = 0;
                    io_pending->pTaskData = task_data;
                    io_pending->dataType = data_type;
                    io_pending->status = 1;

                    task->m_pPending[dir] = io_pending;

                    dev_dbg(&poc->pdev->dev,
                            "CTask_BuildIoBlockYUVRAS(%d) V HostAddr(0x%p) ulArmAddress(0x%x) len(%d)\n",
                            data_type, io_pending->pHostAddress,
                            io_pending->ulArmAddress, io_pending->ulLength);
                }
            } else {
                /* UV Plane */
                if (task->m_dwMaxDMASize < uv_remaining)
                    uv_remaining = task->m_dwMaxDMASize;

                io_pending->pHostAddress = (void *)(task_data->UserBuffer[data_type]
                .BUFFER.YUV.pUVBuffer + uv_offset);
                io_pending->ulArmAddress = (uv_arm_var >> 2) + uv_arm_base;
                io_pending->ulLength = uv_remaining;
                io_pending->ulXferMode = xfer_mode;
                io_pending->ulPicWidth = 0;
                io_pending->ulPicHeight = 0;
                io_pending->pTaskData = task_data;
                io_pending->dataType = data_type;
                io_pending->status = 1;

                task->m_pPending[dir] = io_pending;

                dev_dbg(&poc->pdev->dev,
                        "CTask_BuildIoBlockYUVRAS(%d) U HostAddr(0x%p) ulArmAddress(0x%x) len(%d)\n",
                        data_type, io_pending->pHostAddress,
                        io_pending->ulArmAddress, io_pending->ulLength);
            }
        } else {
            /* Y Plane */
            if (task->m_dwMaxDMASize < y_remaining)
                y_remaining = task->m_dwMaxDMASize;

            io_pending->pHostAddress = (void *)(task_data->UserBuffer[data_type]
            .BUFFER.YUV.pYBuffer + y_offset);
            io_pending->ulArmAddress = (y_arm_var >> 2) + y_arm_base;
            io_pending->ulLength = y_remaining;
            io_pending->ulXferMode = xfer_mode;
            io_pending->ulPicWidth = 0;
            io_pending->ulPicHeight = 0;
            io_pending->pTaskData = task_data;
            io_pending->dataType = data_type;
            io_pending->status = 1;

            task->m_pPending[dir] = io_pending;

            dev_dbg(&poc->pdev->dev,
                    "CTask_BuildIoBlockYUVRAS(%d) Y HostAddr(0x%p) ulArmAddress(0x%x) len(%d)\n",
                    data_type, io_pending->pHostAddress,
                    io_pending->ulArmAddress, io_pending->ulLength);
        }
    }

    /* Leave Critical Section */
    spin_unlock_irqrestore(&task->m_CritSectionIOPending.m_spinlock, flags);
}

int CTask_BuildIoBlockSideBandDMA(struct c_task *task, struct task_dma_request *req)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_BuildIoBlockSideBandDMA() - NEEDS IMPLEMENTING\n");
    return 0;
}

void CTask_SendIoBlockSideBandDMA(struct c_task *task, struct task_dma_request *req)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_SendIoBlockSideBandDMA() - NEEDS IMPLEMENTING\n");
}

