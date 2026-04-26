// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_process.c - IO completion and data streaming processing
 */
#include "ctask.h"
#include "ctask_private.h"
#include "../queue.h"


/* ================================================================
 * CTask_CompleteData
 * ================================================================ */
void CTask_CompleteData(struct c_task *task, enum channel_direction dir)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    u32 length;
    enum task_data_type data_type;
    struct task_data *td;
    struct task_dma_request *dma_req;

    dev_dbg(&poc->pdev->dev,
            "CTask_CompleteData() dir(%d) pending(0x%p)\n",
            dir, task->m_pPending[(int)dir]);

    if (!task->m_pPending[(int)dir])
        return;

    length = task->m_pPending[(int)dir]->ulLength;
    data_type = task->m_pPending[(int)dir]->dataType;
    td = task->m_pPending[(int)dir]->pTaskData;
    dma_req = task->m_pDmaRequest;

    if (!td && !dma_req) {
        dev_err(&poc->pdev->dev,
                "CTask_CompleteData() dir(%d) pTaskData(0x%p) m_pDmaRequest(0x%p)\n",
                dir, td, dma_req);
        return;
    }

    if (length > 3) {
        if (dir == CHANNEL_DIR_READ) {
            if (task->m_pMpegCodec->m_hci.DMAReadDone)
                ((int (*)(struct ihciapi *))
                task->m_pMpegCodec->m_hci.DMAReadDone)(&task->m_pMpegCodec->m_hci);
            if (task->m_pMpegCodec->m_hci.CopyFromCommonBuffer)
                ((int (*)(struct ihciapi *, u8 *, u32))
                task->m_pMpegCodec->m_hci.CopyFromCommonBuffer)(
                    &task->m_pMpegCodec->m_hci,
                    task->m_pPending[0]->pHostAddress,
                    task->m_pPending[0]->ulLength);
        } else {
            if (task->m_pMpegCodec->m_hci.DMAWriteDone)
                ((int (*)(struct ihciapi *))
                task->m_pMpegCodec->m_hci.DMAWriteDone)(&task->m_pMpegCodec->m_hci);
        }
    }

    if (!td) {
        /* Side-band DMA */
        if (dma_req)
            dma_req->ulOffset += length;
    } else {
        /* Update offsets based on buffer type */
        int idx = (int)data_type;

        switch (td->ArmRequest[idx].ArmBuffer.type) {
            case ARM_BUF_OTHERS:
                td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 += length;
                td->UserBuffer[idx].pBufDesc->ulBufferOffset += length;
                td->UserBuffer[idx].pBufDesc->ulTotalUsed += length;
                break;

            case ARM_BUF_YUV:
                if (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 <
                    td->UserBuffer[idx].BUFFER.YUV.ulYLength)
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 += length;
                else
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved5 += length;
            td->UserBuffer[idx].pBufDesc->ulBufferOffset += length;
            td->UserBuffer[idx].pBufDesc->ulTotalUsed += length;
            break;

            case ARM_BUF_YUVMB2RAS:
            case ARM_BUF_YUVRAS:
                if (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 <
                    td->UserBuffer[idx].BUFFER.YUV.ulYLength)
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 += length;
                else if (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved5 <
                    td->UserBuffer[idx].BUFFER.YUV.ulUVLength)
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved5 += length;
                else
                    td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved6 += length;
            td->UserBuffer[idx].pBufDesc->ulBufferOffset += length;
            td->UserBuffer[idx].pBufDesc->ulTotalUsed += length;
            break;

            default:
                dev_dbg(&poc->pdev->dev,
                        "CTask_CompleteData() Unknown buffer type(%d)\n",
                        td->ArmRequest[idx].ArmBuffer.type);
                break;
        }
    }

    if (task->ProcessIoComplete)
        ((void (*)(struct c_task *, enum channel_direction))
        task->ProcessIoComplete)(task, dir);
}

/* ================================================================
 * CTask_ProcessIoComplete
 * ================================================================ */
void CTask_ProcessIoComplete(struct c_task *task, enum channel_direction dir)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td = NULL;
    enum task_data_type data_type;

    dev_dbg(&poc->pdev->dev, "CTask_ProcessIoComplete() %s\n",
            (dir == CHANNEL_DIR_READ) ? "READ" : "WRITE");

    CObject_enterCritical(&task->m_CritSectionIOPending);

    if (task->m_pPending[(int)dir]) {
        td = task->m_pPending[(int)dir]->pTaskData;
        data_type = task->m_pPending[(int)dir]->dataType;
        task->m_pPending[(int)dir] = NULL;
        memset(&task->m_ioPending[(int)dir], 0, sizeof(struct task_io_pending));
    }

    CObject_leaveCritical(&task->m_CritSectionIOPending);

    if (!td) {
        if (task->m_pDmaRequest) {
            if (task->m_pDmaRequest->ulOffset == task->m_pDmaRequest->ulLength) {
                task->m_pDmaRequest = NULL;
                QPOSMSetEvtgrp(&task->m_EvtDmaReqComplete, 1);
            }
            if (task->ProcessDataStreaming)
                ((void (*)(struct c_task *))task->ProcessDataStreaming)(task);
        }
    } else if (td->type == TASK_TYPE_ENC) {
        CEncoderTask_ProcessIoComplete(task, td, data_type, 1);
    } else {
        CDecoderTask_ProcessIoComplete(task, td, data_type, 1);
    }
}

void CTask_ProcessDataStreaming(struct c_task *task)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td;
    struct c_channel *channel;
    enum task_data_type data_type;
    int i, j;
    int rd_ready, wr_ready;

    rd_ready = CTask_isIOReady(task, CHANNEL_DIR_READ);
    wr_ready = CTask_isIOReady(task, CHANNEL_DIR_WRITE);

    dev_dbg(&poc->pdev->dev,
            "CTask_ProcessDataStreaming() rd_ready(%d) wr_ready(%d)\n",
            rd_ready, wr_ready);

    /* Handle AES (compressed audio) output if active */
    if (task->m_taskIdAESOut != 0xFFFFFFFF) {
        td = &task->m_TaskData[task->m_taskIdAESOut];
        if (td->valid != 0) {
            CObject_Lock(&td->m_Object);

            if (td->m_State != TASK_STATE_IDLE &&
                td->pChannel[TASK_DATA_TYPE_COMP_AUD] != NULL &&
                td->pArmMsgFifo[TASK_DATA_TYPE_COMP_AUD] != NULL &&
                CTask_isIOReady(task, td->direction[TASK_DATA_TYPE_COMP_AUD])) {

                /* Get ARM request from FIFO if none pending */
                if (td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.valid == 0) {
                    CFifo_GetFifo(td->pArmMsgFifo[TASK_DATA_TYPE_COMP_AUD],
                                  &td->ArmRequest[TASK_DATA_TYPE_COMP_AUD]);
                }

                if (td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.valid != 0 &&
                    td->bFlushing[TASK_DATA_TYPE_COMP_AUD] == 0) {

                    channel = td->pChannel[TASK_DATA_TYPE_COMP_AUD];

                /* Get user buffer if none */
                if (td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc == NULL && channel->GetBuffer) {
                    int ret = ((int (*)(struct c_channel *, struct qp_buffer_descriptor **,
                                        u8 **, u32 *))channel->GetBuffer)(
                                            channel,
                                            &td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc,
                                            &td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].BUFFER.OTHERS.pBuffer,
                                            &td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].BUFFER.OTHERS.ulLength);
                                        if (ret == 0) {
                                            dev_dbg(&poc->pdev->dev,
                                                    "CTask_ProcessDataStreaming() no buffer available on task(%d) data(%d)\n",
                                                    task->m_taskIdAESOut, TASK_DATA_TYPE_COMP_AUD);
                                        }
                }

                if (td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc != NULL) {
                    /* Handle PTS */
                    if (td->direction[TASK_DATA_TYPE_COMP_AUD] == CHANNEL_DIR_READ &&
                        td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.ulPTSValid != 0 &&
                        (td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc->ulFlags & 0x40000) == 0) {
                        td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc->ulPTS =
                        (u64)(td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.ulPTS << 2);
                    td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc->ulFlags |= 0x40000;
                    td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.ulPTSValid = 0;
                        }

                        /* Set wrap flag */
                        if (td->ArmRequest[TASK_DATA_TYPE_COMP_AUD].ArmBuffer.BUFFER.ALL.reserved9 != 0) {
                            td->UserBuffer[TASK_DATA_TYPE_COMP_AUD].pBufDesc->ulFlags |= 4;
                        }

                        CTask_BuildIoBlock(task, td, TASK_DATA_TYPE_COMP_AUD);
                        CTask_SendIoBlock(task, td, TASK_DATA_TYPE_COMP_AUD);

                        if (!CTask_isIOReady(task, CHANNEL_DIR_READ) &&
                            !CTask_isIOReady(task, CHANNEL_DIR_WRITE)) {
                            CObject_Unlock(&td->m_Object);
                        return;
                            }
                }
                    }
                }
                CObject_Unlock(&td->m_Object);
        }
    }
    /* Handle PCM output if active and AES not active */
    else if (task->m_taskIdPCMOut != 0xFFFFFFFF) {
        td = &task->m_TaskData[task->m_taskIdPCMOut];
        if (td->valid != 0) {
            CObject_Lock(&td->m_Object);

            if (td->m_State != TASK_STATE_IDLE &&
                td->pChannel[TASK_DATA_TYPE_PCM] != NULL &&
                td->pArmMsgFifo[TASK_DATA_TYPE_PCM] != NULL &&
                CTask_isIOReady(task, td->direction[TASK_DATA_TYPE_PCM])) {

                if (td->ArmRequest[TASK_DATA_TYPE_PCM].ArmBuffer.BUFFER.ALL.valid == 0) {
                    CFifo_GetFifo(td->pArmMsgFifo[TASK_DATA_TYPE_PCM],
                                  &td->ArmRequest[TASK_DATA_TYPE_PCM]);
                }

                if (td->ArmRequest[TASK_DATA_TYPE_PCM].ArmBuffer.BUFFER.ALL.valid != 0 &&
                    td->bFlushing[TASK_DATA_TYPE_PCM] == 0) {

                    channel = td->pChannel[TASK_DATA_TYPE_PCM];

                if (td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc == NULL && channel->GetBuffer) {
                    int ret = ((int (*)(struct c_channel *, struct qp_buffer_descriptor **,
                                        u8 **, u32 *))channel->GetBuffer)(
                                            channel,
                                            &td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc,
                                            &td->UserBuffer[TASK_DATA_TYPE_PCM].BUFFER.OTHERS.pBuffer,
                                            &td->UserBuffer[TASK_DATA_TYPE_PCM].BUFFER.OTHERS.ulLength);
                                        if (ret == 0) {
                                            dev_dbg(&poc->pdev->dev,
                                                    "CTask_ProcessDataStreaming() no buffer available on task(%d) data(%d)\n",
                                                    task->m_taskIdPCMOut, TASK_DATA_TYPE_PCM);
                                        }
                }

                if (td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc != NULL) {
                    if (td->direction[TASK_DATA_TYPE_PCM] == CHANNEL_DIR_READ &&
                        td->ArmRequest[TASK_DATA_TYPE_PCM].ArmBuffer.BUFFER.ALL.ulPTSValid != 0 &&
                        (td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc->ulFlags & 0x40000) == 0) {
                        td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc->ulPTS =
                        (u64)(td->ArmRequest[TASK_DATA_TYPE_PCM].ArmBuffer.BUFFER.ALL.ulPTS << 2);
                    td->UserBuffer[TASK_DATA_TYPE_PCM].pBufDesc->ulFlags |= 0x40000;
                    td->ArmRequest[TASK_DATA_TYPE_PCM].ArmBuffer.BUFFER.ALL.ulPTSValid = 0;
                        }

                        CTask_BuildIoBlock(task, td, TASK_DATA_TYPE_PCM);
                        CTask_SendIoBlock(task, td, TASK_DATA_TYPE_PCM);

                        if (!CTask_isIOReady(task, CHANNEL_DIR_READ) &&
                            !CTask_isIOReady(task, CHANNEL_DIR_WRITE)) {
                            CObject_Unlock(&td->m_Object);
                        return;
                            }
                }
                    }
                }
                CObject_Unlock(&td->m_Object);
        }
    }

    /* Handle side-band DMA request */
    if (task->m_pDmaRequest != NULL) {
        if (CTask_BuildIoBlockSideBandDMA(task, task->m_pDmaRequest)) {
            CTask_SendIoBlockSideBandDMA(task, task->m_pDmaRequest);
        }
        if (!CTask_isIOReady(task, CHANNEL_DIR_READ) &&
            !CTask_isIOReady(task, CHANNEL_DIR_WRITE)) {
            return;
            }
    }

    /* Main loop - iterate through all tasks */
    for (i = 0; i < 8; i++) {
        int task_idx = (task->m_StartID + i) & 7;
        td = &task->m_TaskData[task_idx];

        dev_info(&poc->pdev->dev,
                 "CTask_ProcessDataStreaming: task_idx=%d valid=%d state=%d m_StartID=%d\n",
                 task_idx, td->valid, td->m_State, td->m_StartID);

        if (td->valid == 0)
            continue;

        CObject_Lock(&td->m_Object);

        if (td->m_State == TASK_STATE_IDLE) {
            /* IDLE - drain all FIFOs */
            for (data_type = TASK_DATA_TYPE_COMP_VID; (int)data_type < 7; data_type++) {
                if (td->pChannel[(int)data_type] != NULL &&
                    td->pArmMsgFifo[(int)data_type] != NULL) {
                    int ret;
                do {
                    if (task->CompleteArm)
                        ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                        task->CompleteArm)(task, td, data_type);
                    ret = CFifo_GetFifo(td->pArmMsgFifo[(int)data_type],
                                        &td->ArmRequest[(int)data_type]);
                } while (ret != 0);
                    }
            }
        } else {
            /* Active - process all data types */
            for (j = 0; j < 7; j++) {
                data_type = (td->m_StartID + j) % 7;
                channel = td->pChannel[(int)data_type];

                dev_info(&poc->pdev->dev,
                         "  data_type=%d channel=%px pArmMsgFifo=%px direction=%d\n",
                         data_type, channel, td->pArmMsgFifo[(int)data_type],
                         td->direction[(int)data_type]);

                if (channel == NULL || td->pArmMsgFifo[(int)data_type] == NULL)
                    continue;

                if (!CTask_isIOReady(task, td->direction[(int)data_type]))
                    continue;

                /* Get ARM request from FIFO if none pending */
                if (td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.valid == 0) {
                    int fifo_ret = CFifo_GetFifo(td->pArmMsgFifo[(int)data_type],
                                                 &td->ArmRequest[(int)data_type]);
                    dev_info(&poc->pdev->dev,
                             "  CFifo_GetFifo returned=%d valid=%d type=%d\n",
                             fifo_ret,
                             td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.valid,
                             td->ArmRequest[(int)data_type].ArmBuffer.type);
                }

                if (td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.valid == 0 ||
                    td->bFlushing[(int)data_type] != 0)
                    continue;

                if (channel->m_pFreeQueue)
                    dev_info(&poc->pdev->dev,
                             "  FreeQueue entries=%d\n",
                             channel->m_pFreeQueue->m_dwNbInQueue);
                if (channel->m_pDataRequestQueue)
                    dev_info(&poc->pdev->dev,
                             "  RequestQueue entries=%d\n",
                             channel->m_pDataRequestQueue->m_dwNbInQueue);
                if (channel->m_pDataPendingQueue)
                    dev_info(&poc->pdev->dev,
                             "  PendingQueue entries=%d\n",
                             channel->m_pDataPendingQueue->m_dwNbInQueue);

                /* Get user buffer if none */
                if (td->UserBuffer[(int)data_type].pBufDesc == NULL) {
                    int ret = 0;

                    switch (td->ArmRequest[(int)data_type].ArmBuffer.type) {
                        case ARM_BUF_YUV:
                            if (channel->GetBufferYUV) {
                                ret = ((int (*)(struct c_channel *, struct qp_buffer_descriptor **,
                                                u8 **, u32 *, u8 **, u32 *))channel->GetBufferYUV)(
                                                    channel,
                                                    &td->UserBuffer[(int)data_type].pBufDesc,
                                                                                                   &td->UserBuffer[(int)data_type].BUFFER.YUV.pYBuffer,
                                                                                                   &td->UserBuffer[(int)data_type].BUFFER.YUV.ulYLength,
                                                                                                   &td->UserBuffer[(int)data_type].BUFFER.YUV.pUVBuffer,
                                                                                                   &td->UserBuffer[(int)data_type].BUFFER.YUV.ulUVLength);
                            }
                            break;

                        case ARM_BUF_YUVMB2RAS:
                        case ARM_BUF_YUVRAS:
                            if (channel->GetBufferYUVRAS) {
                                ret = ((int (*)(struct c_channel *, struct qp_buffer_descriptor **,
                                                u8 **, u32 *, u8 **, u32 *, u8 **, u32 *))channel->GetBufferYUVRAS)(
                                                    channel,
                                                    &td->UserBuffer[(int)data_type].pBufDesc,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.pYBuffer,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.ulYLength,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.pUVBuffer,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.ulUVLength,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.reserved1,
                                                                                                                    &td->UserBuffer[(int)data_type].BUFFER.YUV.reserved2);
                            }
                            break;

                        default:
                            if (channel->GetBuffer) {
                                ret = ((int (*)(struct c_channel *, struct qp_buffer_descriptor **,
                                                u8 **, u32 *))channel->GetBuffer)(
                                                    channel,
                                                    &td->UserBuffer[(int)data_type].pBufDesc,
                                                                                  &td->UserBuffer[(int)data_type].BUFFER.OTHERS.pBuffer,
                                                                                  &td->UserBuffer[(int)data_type].BUFFER.OTHERS.ulLength);
                            }
                            break;
                    }

                    if (ret == 0) {
                        dev_dbg(&poc->pdev->dev,
                                "CTask_ProcessDataStreaming() no buffer on task(%d) data(%d)\n",
                                task_idx, data_type);
                        continue;
                    }

                    dev_info(&poc->pdev->dev,
                             "  GetBuffer success: pBufDesc=%px type=%d\n",
                             td->UserBuffer[(int)data_type].pBufDesc,
                             td->ArmRequest[(int)data_type].ArmBuffer.type);
                }

                if (td->UserBuffer[(int)data_type].pBufDesc == NULL)
                    continue;

                /* Handle PTS for writes */
                if (td->direction[(int)data_type] == CHANNEL_DIR_WRITE &&
                    (td->UserBuffer[(int)data_type].pBufDesc->ulFlags & 0x40000) != 0 &&
                    td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTSValid == 0) {
                    td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTSValid = 1;
                td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTS =
                (u32)(td->UserBuffer[(int)data_type].pBufDesc->ulPTS >> 2);
                    }

                    /* Handle PTS for reads */
                    if (td->direction[(int)data_type] == CHANNEL_DIR_READ &&
                        td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTSValid != 0 &&
                        (td->UserBuffer[(int)data_type].pBufDesc->ulFlags & 0x40000) == 0) {
                        td->UserBuffer[(int)data_type].pBufDesc->ulPTS =
                        (u64)(td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTS << 2);
                    td->UserBuffer[(int)data_type].pBufDesc->ulFlags |= 0x40000;
                    td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.ulPTSValid = 0;
                        }

                        /* Build and send IO block based on buffer type */
                        switch (td->ArmRequest[(int)data_type].ArmBuffer.type) {
                            case ARM_BUF_YUV:
                                if (td->m_State != TASK_STATE_RUN &&
                                    td->direction[(int)data_type] == CHANNEL_DIR_READ)
                                    continue;
                                CTask_BuildIoBlockYUV(task, td, data_type);
                                break;

                            case ARM_BUF_YUVMB2RAS:
                                if (td->m_State != TASK_STATE_RUN &&
                                    td->direction[(int)data_type] == CHANNEL_DIR_READ)
                                    continue;
                                CTask_BuildIoBlockYUVMB2RAS(task, td, data_type);
                                break;

                            case ARM_BUF_YUVRAS:
                                if (td->m_State != TASK_STATE_RUN &&
                                    td->direction[(int)data_type] == CHANNEL_DIR_READ)
                                    continue;
                                CTask_BuildIoBlockYUVRAS(task, td, data_type);
                                break;

                            default:
                                if (td->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.reserved9 != 0) {
                                    td->UserBuffer[(int)data_type].pBufDesc->ulFlags |= 4;
                                }
                                CTask_BuildIoBlock(task, td, data_type);
                                break;
                        }

                        CTask_SendIoBlock(task, td, data_type);

                        if (!CTask_isIOReady(task, CHANNEL_DIR_READ) &&
                            !CTask_isIOReady(task, CHANNEL_DIR_WRITE)) {
                            CObject_Unlock(&td->m_Object);
                        return;
                            }
            }
        }

        CObject_Unlock(&td->m_Object);
    }

    dev_dbg(&poc->pdev->dev, "CTask_ProcessDataStreaming() done\n");
}

/* ================================================================
 * Stubs
 * ================================================================ */
int CTask_ProcessCancelBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! task_id=%u data_type=%d\n",
             __func__, task_id, data_type);
    return 0;
}

int CTask_ProcessFlush(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! task_id=%u data_type=%d\n",
             __func__, task_id, data_type);
    return 0;
}

int CTask_ProcessFlushArm(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! task_id=%u data_type=%d\n",
             __func__, task_id, data_type);
    return 0;
}

/*
 * CEncoderTask_ProcessIoComplete
 *
 * Called when a DMA transfer completes. Handles buffer recycling
 * and continues the DMA pipeline for YUV input.
 */
void CEncoderTask_ProcessIoComplete(struct c_task *task, struct task_data *task_data,
                                    enum task_data_type data_type, int param_4)
{
    struct c985_poc *poc;
    u32 uVar1;
    struct qp_buffer_descriptor *buf_desc;

    if (!task || !task_data)
        return;

    poc = codec_to_poc(task->m_pMpegCodec);

    /* Match decompilation debug print */
    dev_dbg(&poc->pdev->dev,
            "CEncoderTask_ProcessIoComplete() Task_id(%d) arm_buf(0x%x) usr_buf(0x%x) usr_offset(%d) usr_size(%d)\n",
            task_data->id,
            &task_data->ArmRequest[data_type],
            &task_data->UserBuffer[data_type],
            task_data->UserBuffer[data_type].pBufDesc ? task_data->UserBuffer[data_type].pBufDesc->ulBufferOffset : 0,
            task_data->UserBuffer[data_type].pBufDesc ? task_data->UserBuffer[data_type].pBufDesc->ulBufferSize : 0);

    /* ============================================
     * PATH 1: YUV INPUT (TASK_DATA_TYPE_YUV = 4)
     * ============================================ */
    if (data_type == TASK_DATA_TYPE_YUV) {
        /* Case 1: ARM buffer already released */
        if (task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.BUFFER.ALL.valid == 0) {
            dev_dbg(&poc->pdev->dev,
                    "CEncoderTask_ProcessIoComplete() ARM (YUV) buffer released already!\n");

            if (task->CompleteUser) {
                ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                task->CompleteUser)(task, task_data, TASK_DATA_TYPE_YUV);
            }
        }
        /* Case 2: Frame Complete Check */
        else {
            int frame_complete = 0;
            u32 arm_type = task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.type;

            /* === SAVE VALUES BEFORE CompleteArm Clears Them === */
            u32 reserved4 = task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.BUFFER.ALL.reserved4;
            u32 reserved5 = task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.BUFFER.ALL.reserved5;
            u32 reserved6 = task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.BUFFER.ALL.reserved6;
            u32 ulYLength = task_data->UserBuffer[TASK_DATA_TYPE_YUV].BUFFER.YUV.ulYLength;
            u32 ulUVLength = task_data->UserBuffer[TASK_DATA_TYPE_YUV].BUFFER.YUV.ulUVLength;
            u32 reserved2 = task_data->UserBuffer[TASK_DATA_TYPE_YUV].BUFFER.YUV.reserved2;

            /* Check based on ArmBuffer.type */
            if (arm_type == ARM_BUF_YUV) {
                if ((reserved4 == ulYLength) && (reserved5 == ulUVLength)) {
                    frame_complete = 1;
                }
            }
            else if (arm_type == ARM_BUF_YUVMB2RAS) {
                if ((reserved4 == ulYLength) && (reserved5 == ulUVLength) && (reserved6 == reserved2)) {
                    frame_complete = 1;
                }
            }
            else if (arm_type == ARM_BUF_YUVRAS) {
                if ((reserved4 == ulYLength) && (reserved5 == ulUVLength) && (reserved6 == reserved2)) {
                    frame_complete = 1;
                }
            }
            else if (arm_type == ARM_BUF_OTHERS) {
                if (reserved4 == (task_data->ArmRequest[TASK_DATA_TYPE_YUV].ArmBuffer.BUFFER.ALL.reserved3 << 2)) {
                    frame_complete = 1;
                }
            }

            if (frame_complete) {
                dev_dbg(&poc->pdev->dev,
                        "CEncoderTask_ProcessIoComplete() YUV Frame Complete (type=%d)\n", arm_type);

                /* Call CompleteArm (this will clear ArmRequest) */
                if (task->CompleteArm) {
                    ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                    task->CompleteArm)(task, task_data, TASK_DATA_TYPE_YUV);
                }

                /* Call CompleteUser */
                if (task->CompleteUser) {
                    ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                    task->CompleteUser)(task, task_data, TASK_DATA_TYPE_YUV);
                }
            }
            else {
                /* Frame in progress - DO NOT call CompleteUser */
                dev_dbg(&poc->pdev->dev,
                        "CEncoderTask_ProcessIoComplete() YUV frame in progress (offset=%u/%u)\n",
                        reserved4, ulYLength);
            }
        }
    }
    /* ============================================
     * PATH 2: OTHER DATA TYPES (compressed audio/video)
     * ============================================ */
    else {
        /* Only process if ARM buffer is valid */
        if (task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.valid == 0) {
            goto check_buffer_full;
        }

        /* Check if transfer is complete */
        if (task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.reserved4 !=
            (task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.reserved3 << 2)) {
            goto check_buffer_full;
            }

            /* === COMPRESSED AUDIO TYPE FLAGS === */
            uVar1 = task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.reserved6;

        if (uVar1 == 1) {
            (task_data->UserBuffer[(int)data_type].pBufDesc)->ulFlags |= 0x1000000;
        }
        else if (uVar1 == 2) {
            (task_data->UserBuffer[(int)data_type].pBufDesc)->ulFlags |= 0x2000000;
        }
        else if (uVar1 == 3) {
            (task_data->UserBuffer[(int)data_type].pBufDesc)->ulFlags |= 0x4000000;
        }
        else if (uVar1 == 4) {
            (task_data->UserBuffer[(int)data_type].pBufDesc)->ulFlags |= 0x8000000;
        }

        dev_dbg(&poc->pdev->dev,
                "%s() compressed audio type (0x%x) ulFlags(0x%08x)\n",
                __func__, uVar1,
                task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags);

        /* === CHECK reserved7 (LAST FRAME FLAG) === */
        if (task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.reserved7 == 0) {
            /* Not last frame - check flags */
            if ((task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags & 0x10000) != 0) {
                /* Check if should complete */
                if ((((task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags & 0x80000) == 0) ||
                    ((task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags & 4) == 0)) ||
                    (task_data->UserBuffer[(int)data_type].pBufDesc->ulBufferSize <=
                    task_data->UserBuffer[(int)data_type].pBufDesc->ulBufferOffset)) {
                    /* Complete user buffer */
                    if (task->CompleteUser) {
                        ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                        task->CompleteUser)(task, task_data, data_type);
                    }
                    }
                    else {
                        /* Clear bit 2 (continue streaming) */
                        task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags &= 0xfffffffb;
                    }
            }
        }
        else {
            /* Last frame - set end-of-stream flag */
            task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags |= 0x20000;

            if (task->CompleteUser) {
                ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                task->CompleteUser)(task, task_data, data_type);
            }
        }

        /* Complete ARM buffer */
        if (task->CompleteArm) {
            ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
            task->CompleteArm)(task, task_data, data_type);
        }

        check_buffer_full:
        /* === CHECK IF BUFFER IS FULL === */
        if ((task_data->UserBuffer[(int)data_type].pBufDesc != NULL) &&
            (task_data->UserBuffer[(int)data_type].pBufDesc->ulBufferOffset ==
            task_data->UserBuffer[(int)data_type].pBufDesc->ulBufferSize)) {

            /* If ARM buffer still valid, set flag 2 */
            if (task_data->ArmRequest[(int)data_type].ArmBuffer.BUFFER.ALL.valid != 0) {
                task_data->UserBuffer[(int)data_type].pBufDesc->ulFlags |= 4;
            }

            /* Complete user buffer */
            if (task->CompleteUser) {
                ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                task->CompleteUser)(task, task_data, data_type);
            }
            }
    }

    /* === TRIGGER DATA STREAMING === */
    if (param_4 != 0) {
        if (task->ProcessDataStreaming) {
            ((void (*)(struct c_task *))task->ProcessDataStreaming)(task);
        }
    }
}

void CDecoderTask_ProcessIoComplete(struct c_task *task, struct task_data *td,
                                    enum task_data_type data_type, int status)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! td=%p data_type=%d status=%d\n",
             __func__, td, data_type, status);
}

int CEncoderTask_Start(struct c_task *task, u32 task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    int ret;

    dev_dbg(&poc->pdev->dev, "CEncoderTask_Start hTask(%u) BEFORE m_State=%d\n",
            task_id, task->m_TaskData[task_id].m_State);

    dev_dbg(&poc->pdev->dev, "CEncoderTask_Start hTask(%u)\n", task_id);

    ret = CQLCodec_UpdateMiscConfig(&poc->codec, task_id);

    if (ret < 0) {
        dev_err(&poc->pdev->dev,
                "CEncoderTask_Start() CQLCodec_UpdateMiscConfig() failed(%d)\n", ret);
        return ret;
    }

    ret = QPFWAPI_MailboxReady(poc, 500);
    if (ret < 0) {
        dev_err(&poc->pdev->dev,
                "CEncoderTask_Start() QPFWAPI_MailboxReady() failed(%d)\n", ret);
        return ret;
    }

    ret = CQLCodec_UpdateEncoderConfig(poc, task_id, 0);
    if (ret >= 0) {
        ret = QPFWENCAPI_StartEncoder(poc, task_id);
    }

    QPFWAPI_MailboxDone(poc);

    if (ret >= 0) {
        task->m_TaskData[task_id].m_State = TASK_STATE_RUN;
        dev_dbg(&poc->pdev->dev, "CEncoderTask_Start hTask(%u) AFTER m_State=%d\n");
        dev_dbg(&poc->pdev->dev, "CEncoderTask_Start() m_State = TASK_STATE_RUN\n");
    }

    return ret;
}
int CDecoderTask_Start(struct c_task *task, u32 task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! task_id=%u\n",
             __func__, task_id);
    return 0;
}

int CTask_FindTaskDataType(struct c_task *task, enum task_type type,
                           u32 id, u32 *task_id, enum task_data_type *data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! type=%d id=%u\n",
             __func__, type, id);
    return 0;
}

int CTask_FindDataType(struct c_task *task, u32 task_id,
                       u32 fw_data_type, enum task_data_type *data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    enum task_data_type local_type;

    for (local_type = TASK_DATA_TYPE_COMP_VID; local_type <= 6; local_type++) {
        if (task->m_TaskData[task_id].pChannel[(int)local_type] != NULL &&
            fw_data_type == task->m_TaskData[task_id].FWDataType[(int)local_type]) {
            *data_type = local_type;
        return 1;
            }
    }

    dev_dbg(&poc->pdev->dev,
            "CTask_FindDataType() hTask(%u): fwDataType(0x%x) not found!!\n",
            task_id, fw_data_type);
    return 0;
}

int CTask_FindTask(struct c_task *task, enum task_type type,
                   enum task_data_type data_type, u32 *task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);

    dev_warn(&poc->pdev->dev,
             "STUB: %s() called - NEEDS IMPLEMENTING! type=%d data_type=%d\n",
             __func__, type, data_type);
    return 0;
}
