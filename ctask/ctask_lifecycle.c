// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_lifecycle.c - Task open, close, start, stop, pause, resume
 */

#include "ctask_private.h"
#include "../firmware.h""
/* ================================================================
 * CTask_Open
 * ================================================================ */
int CTask_Open(struct c_task *task, u32 task_id, enum task_data_type data_type,
               enum channel_direction dir, u32 fw_data_type, struct c_channel *channel)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td;
    struct c_fifo *fifo;
    int ret = 0;
    int i;

    dev_dbg(&poc->pdev->dev, "CTask_Open() hTask(%u) dataType(%d) dir(%d)\n",
            task_id, data_type, dir);

    if (task_id >= MAX_TASKS || (int)data_type >= TASK_DATA_TYPE_MAX)
        return -EINVAL;

    td = &task->m_TaskData[task_id];

    /* FIX #2: Acquire task data lock BEFORE checking validity */
    CObject_lock(&td->m_Object);

    if (data_type != TASK_DATA_TYPE_INVALID) {
        /* Check if task is valid and channel not already open */
        if (td->valid == 0 || td->pChannel[(int)data_type] != NULL) {
            CObject_unlock(&td->m_Object);  /* FIX #2: Unlock on early exit */
            return -EBUSY;
        }

        /* Allocate ARM message FIFO */
        fifo = kzalloc(sizeof(*fifo), GFP_KERNEL);
        if (!fifo) {
            CObject_unlock(&td->m_Object);  /* FIX #2: Unlock on allocation failure */
            return -ENOMEM;
        }

        fifo = CFifo_Constructor(fifo, &task->m_Object, 2, 0x201, 0x48);
        td->pArmMsgFifo[(int)data_type] = fifo;

        if (!td->pArmMsgFifo[(int)data_type]) {
            kfree(fifo);
            CObject_unlock(&td->m_Object);  /* FIX #2: Unlock on constructor failure */
            return -ENOMEM;
        }
    }

    /* Clear user buffer and ARM request */
    memset(&td->UserBuffer[(int)data_type], 0, sizeof(struct task_user_buffer));
    memset(&td->ArmRequest[(int)data_type], 0, sizeof(struct task_arm_request));

    td->ArmRequestNumber[(int)data_type] = 0;
    td->pBufDescToCancel[(int)data_type] = NULL;
    td->bFlushing[(int)data_type] = 0;

    /* Clear ARM buffer address for encoder compressed video */
    if (data_type == TASK_DATA_TYPE_COMP_VID && td->type == TASK_TYPE_ENC)
        td->ArmBufferAddr = 0;

    /* Set channel info */
    td->pChannel[(int)data_type] = channel;
    td->direction[(int)data_type] = dir;
    td->FWDataType[(int)data_type] = fw_data_type;

    /* First time opening this task? */
    if (td->m_dwSession == 0) {
        td->m_bAcquired = 0;

        /* Call firmware system open */
        ret = QPFWCODECAPI_SystemOpen(task->m_pMpegCodec, task_id, td->m_codec_function);
        if (ret < 0) {
            /* Cleanup on failure */
            if (td->pArmMsgFifo[(int)data_type]) {
                CFifo_Destructor(td->pArmMsgFifo[(int)data_type]);
                kfree(td->pArmMsgFifo[(int)data_type]);
                td->pArmMsgFifo[(int)data_type] = NULL;
            }
            CObject_unlock(&td->m_Object);  /* FIX #2: Unlock on firmware open failure */
            return ret;
        }

        /* FIX #2: Lock task object (not task data) for session check */
        CObject_lock(&task->m_Object);

        /* Check if this is the first active session across all tasks */
        for (i = 0; i < MAX_TASKS; i++) {
            if (task->m_TaskData[i].valid != 0 &&
                task->m_TaskData[i].m_dwSession != 0)
                break;
        }

        /* First active session - start thread and enable interrupts */
        if (i == MAX_TASKS) {
            int j;

            /* Clear IO pending */
            for (j = 0; j < 2; j++) {
                memset(&task->m_ioPending[j], 0, sizeof(struct task_io_pending));
                task->m_pPending[j] = NULL;
            }

            /* FIX #1: Clear event group BEFORE thread initialization */
            if (task->m_pMpegCodec->m_EvtTask)
                QPOSMClearEvtgrp(task->m_pMpegCodec->m_EvtTask, 0x1ff);

            /* Initialize thread */
            ret = CThread_ThreadInit(&task->m_Thread);
            if (ret != 0) {  /* FIX: Check for failure (0 = success in decompiled) */
                dev_err(&poc->pdev->dev, "CTask_Open() CThread_ThreadInit() failed!\n");
                CObject_unlock(&task->m_Object);
                CObject_unlock(&td->m_Object);
                return -EIO;
            }

            /* Enable interrupts */
            if (task->m_pMpegCodec->m_hci.SetInterrupt)
                ((int (*)(struct ihciapi *, u32))task->m_pMpegCodec->m_hci.SetInterrupt)(
                    &task->m_pMpegCodec->m_hci, 7);
        }

        CObject_unlock(&task->m_Object);
    }

    /* Mark session as active for this data type */
    td->m_dwSession |= (1 << ((int)data_type & 0x1F));

    CObject_unlock(&td->m_Object);  /* FIX #2: Final unlock of task data object */

    return ret;
}

/* ================================================================
 * CTask_Close
 * ================================================================ */
int CTask_Close(struct c_task *task, u32 task_id, enum task_data_type data_type,
                int lock_main)
{
    struct cql_codec *codec = task->m_pMpegCodec;
    struct c985_poc *poc = container_of(codec, struct c985_poc, codec); // Fix this line
    struct task_data *td;
    int i, j;
    int ret;

    dev_dbg(&poc->pdev->dev, "CTask_Close() hTask(%u) dataType(%d)\n",
            task_id, data_type);

    if (task_id >= MAX_TASKS || (int)data_type >= TASK_DATA_TYPE_MAX + 1)
        return QPERR_PARMS;

    td = &task->m_TaskData[task_id];

    /* Virtual data type is always success */
    if (data_type == TASK_DATA_TYPE_VIRTUAL)
        return QPERR_SUCCESS;

    if (td->valid == 0 || td->pChannel[(int)data_type] == NULL)
        return QPERR_INVALID;

    /* Lock task data object */
    CObject_Lock(&td->m_Object);

    /* Clear this data type from session mask */
    td->m_dwSession &= ~(1 << ((int)data_type & 0x1F));

    /* Complete any pending ARM and user buffers */
    if (task->CompleteArm)
        ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
        task->CompleteArm)(task, td, data_type);
    if (task->CompleteUser)
        ((int (*)(struct c_task *, struct task_data *, enum task_data_type))
        task->CompleteUser)(task, td, data_type);

    /* Clear channel */
    td->pChannel[(int)data_type] = NULL;

    /* Drain and destroy ARM message FIFO */
    if (td->pArmMsgFifo[(int)data_type]) {
        do {
            if (task->CompleteArm)
                ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                task->CompleteArm)(task, td, data_type);
            ret = CFifo_GetFifo(td->pArmMsgFifo[(int)data_type],
                                &td->ArmRequest[(int)data_type]);
        } while (ret != 0);

        CFifo_Destructor(td->pArmMsgFifo[(int)data_type]);
        kfree(td->pArmMsgFifo[(int)data_type]);
        td->pArmMsgFifo[(int)data_type] = NULL;
    }

    /* If all sessions closed, close the task */
    if (td->m_dwSession == 0) {
        QPFWCODECAPI_SystemClose(task->m_pMpegCodec, task_id);

        if (lock_main)
            CObject_Lock(&task->m_Object);

        /* Check if any tasks still have active sessions */
        for (i = 0; i < MAX_TASKS; i++) {
            if (task->m_TaskData[i].valid != 0 &&
                task->m_TaskData[i].m_dwSession != 0)
                break;
        }

        /* No more active tasks - shutdown */
        if (i == MAX_TASKS) {
            /* Disable interrupts */
            if (task->m_pMpegCodec->m_hci.DisableInterrupts)
                ((int (*)(struct ihciapi *, u32))
                task->m_pMpegCodec->m_hci.DisableInterrupts)(
                    &task->m_pMpegCodec->m_hci, 7);

                /* Stop thread */
                CThread_ThreadDone(&task->m_Thread);

            /* Clear IO pending */
            for (j = 0; j < 2; j++) {
                memset(&task->m_ioPending[j], 0, sizeof(struct task_io_pending));
                task->m_pPending[j] = NULL;
            }

            /* Wake up any DMA waiters */
            if (task->m_pDmaRequest) {
                task->m_pDmaRequest = NULL;
                QPOSMSetEvtgrp(&task->m_EvtDmaReqComplete, 1);
            }

            if (codec->m_ErrorRecovery == 2 ||
                (CQLCodec_IsCodecError(codec) &&
                codec->m_ErrorRecovery == 1)) {
                ret = CQLCodec_FWDownloadAll(poc, 0, 1);
            if (ret < 0) {
                dev_err(&poc->pdev->dev,
                        "CTask_Close() FWDownload Failed status(%d)!!!\n", ret);
            } else {
                CQLCodec_ClrCodecError(codec);
            }
                }
        }

        if (lock_main)
            CObject_Unlock(&task->m_Object);
    }

    /* Unlock task data object */
    CObject_Unlock(&td->m_Object);

    return QPERR_SUCCESS;
}

int CTask_Start(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td;
    struct cql_codec_lib *codec_lib;
    int ret = 0;
    int use_vid, need_vid;
    u32 frame_rate;
    u32 width, height;
    u32 input_format;

    dev_dbg(&poc->pdev->dev, "CTask_Start() hTask(%u) dataType(%d)\n",
            task_id, data_type);

    if (task_id >= 9 || (int)data_type >= 8)
        return QPERR_PARMS;

    td = &task->m_TaskData[task_id];

    if (td->valid == 0 || td->pChannel[(int)data_type] == NULL)
        return QPERR_INVALID;

    /* Video input device handling */
    use_vid = CTask_UseVideoInputDevice(task, td);
    if (use_vid != 0) {
        need_vid = CTask_NeedVideoInputDevice(task);
        if (need_vid == 0) {
            codec_lib = (struct cql_codec_lib *)task->m_pMpegCodec->m_Object.m_pParent;

            /* Calculate frame rate */
            if ((td->m_inputControl & 0x400000) == 0) {
                u32 input_type = (td->m_inputControl >> 16) & 0x7f;
                if (input_type == 0 || input_type > 2)
                    frame_rate = 30; /* 0x1e */
                    else
                        frame_rate = 25; /* 0x19 */
            } else {
                frame_rate = (td->m_inputControl >> 16) & 0x3f;
            }

            /* Set video data type based on codec function */
            if (((td->m_codec_function >> 8) & 1) == 1 && td->m_video_input == 0) {
                if ((td->m_codec_function & 0xf) == 2) {
                    /* MJPEG - set video data type 2 */
                    if (codec_lib->m_pVidDecoder) {
                        /* codec_lib->m_pVidDecoder->SetVideoDataType(2) */
                    }
                }
            } else {
                /* Set video data type 3 */
                if (codec_lib->m_pVidDecoder) {
                    /* codec_lib->m_pVidDecoder->SetVideoDataType(3) */
                }
            }

            /* Extract resolution */
            width = td->m_pictureResolution & 0x7ff;
            height = (td->m_pictureResolution >> 16) & 0x7ff;
            input_format = td->m_inputControl & 7;

            /* SetVideoResolution and SetFrameRate calls would go here */
            dev_dbg(&poc->pdev->dev,
                    "CTask_Start() vid setup: %ux%u fmt=%u rate=%u\n",
                    width, height, input_format, frame_rate);
        }
    }

    /* Lock task data */
    CObject_Lock(&td->m_Object);

    /* Only start if not already started */
    if (td->m_dwStarted == 0) {
        if (td->type == TASK_TYPE_ENC) {
            ret = CEncoderTask_Start(task, task_id);
        } else {
            ret = CDecoderTask_Start(task, task_id);
        }
        td->m_Error = ret;
    }

    if (ret >= 0) {
        td->m_dwStarted |= (1 << ((u8)data_type & 0x1F));

        if (data_type == TASK_DATA_TYPE_COMP_AUD &&
            td->direction[1] == CHANNEL_DIR_READ) {
            task->m_taskIdAESOut = task_id;
        dev_dbg(&poc->pdev->dev,
                "CTask_Start() assign m_taskIdAESOut to hTask(%u)\n", task_id);
            } else if (data_type == TASK_DATA_TYPE_PCM &&
                td->direction[5] == CHANNEL_DIR_READ) {
                task->m_taskIdPCMOut = task_id;
            dev_dbg(&poc->pdev->dev,
                    "CTask_Start() assign m_taskIdPCMOut to hTask(%u)\n", task_id);
                }
    }

    /* Unlock task data */
    CObject_Unlock(&td->m_Object);

    return ret;
}

int CTask_Acquire(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td;
    int ret = 0;

    dev_dbg(&poc->pdev->dev, "CTask_Acquire() hTask(%u) dataType(%d)\n",
            task_id, data_type);

    if (task_id >= MAX_TASKS || (int)data_type >= TASK_DATA_TYPE_MAX)
        return QPERR_PARMS;

    td = &task->m_TaskData[task_id];

    if (td->valid == 0 || td->pChannel[(int)data_type] == NULL) {
        dev_dbg(&poc->pdev->dev, "CTask_Acquire() invalid: valid=%d pChannel=%p\n",
                td->valid, td->pChannel[(int)data_type]);
        return QPERR_INVALID;
    }

    CObject_Lock(&td->m_Object);

    dev_dbg(&poc->pdev->dev, "CTask_Acquire() m_bAcquired=%d vin=%d vout=%d ain=%d aout=%d\n",
            td->m_bAcquired, td->m_video_input, td->m_video_output,
            td->m_audio_input, td->m_audio_output);

    if (td->m_bAcquired == 0) {
        ret = QPFWCODECAPI_SystemLink(
            task->m_pMpegCodec,
            task_id,
            td->m_video_input,
            td->m_video_in_ch,
            td->m_video_output,
            td->m_video_out_ch,
            td->m_audio_input,
            td->m_audio_in_ch,
            td->m_audio_output,
            td->m_audio_out_ch
        );

        dev_dbg(&poc->pdev->dev, "CTask_Acquire() SystemLink returned %d\n", ret);

        if (ret >= 0) {
            td->m_bAcquired = 1;
        }
    }

    CObject_Unlock(&td->m_Object);

    return ret;
}

/* ================================================================
 * Stubs - TODO: implement from Ghidra
 * ================================================================ */

int CTask_Stop(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_Stop() hTask(%u) dataType(%d) - NEEDS IMPLEMENTING\n",
            task_id, data_type);
    return 0;
}

int CTask_Pause(struct c_task *task, u32 task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_Pause() hTask(%u) - NEEDS IMPLEMENTING\n", task_id);
    return 0;
}

int CTask_Resume(struct c_task *task, u32 task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_Resume() hTask(%u) - NEEDS IMPLEMENTING\n", task_id);
    return 0;
}

int CTask_CancelBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_CancelBuffer() hTask(%u) dataType(%d) - NEEDS IMPLEMENTING\n",
            task_id, data_type);
    return 0;
}

int CTask_NewBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_NewBuffer() hTask(%u) dataType(%d) - NEEDS IMPLEMENTING\n",
            task_id, data_type);
    return 0;
}

int CTask_Flush(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct cql_codec *codec;
    struct c985_poc *poc = NULL;
    struct task_data *td;
    enum channel_direction dir;
    int is_pending;
    int ret;
    unsigned long timeout;

    /* Get device context for logging */
    if (task && task->m_pMpegCodec) {
        codec = task->m_pMpegCodec;
        poc = container_of(codec, struct c985_poc, codec);
        dev_dbg(&poc->pdev->dev, "CTask_Flush() hTask(%u) dataType(%d)[\n",
                task_id, data_type);
    }

    /* Validate parameters */
    if (task_id >= MAX_TASKS || (int)data_type >= TASK_DATA_TYPE_MAX)
        return 0;

    td = &task->m_TaskData[task_id];

    if (td->valid == 0 || td->pChannel[(int)data_type] == NULL)
        return 0;

    /* Clear flush reply event (bit 2) */
    if (td->m_EvtReply)
        reinit_completion(&td->m_EvtReply->events[2]);

    /* Set flushing flag */
    td->bFlushing[(int)data_type] = 1;

    /* Get direction for this data type */
    dir = td->direction[(int)data_type];

    /* Check if IO is pending for this direction */
    CObject_Lock(&task->m_CritSectionIOPending);
    is_pending = 0;
    if (task->m_pPending[(int)dir] != NULL &&
        task->m_pPending[(int)dir]->pTaskData == td) {
        is_pending = 1;
        }
        CObject_Unlock(&task->m_CritSectionIOPending);

    if (is_pending) {
        /* Wait for DMA to complete (timeout 1000ms) */
        timeout = jiffies + msecs_to_jiffies(1000);

        while (time_before(jiffies, timeout)) {
            CObject_Lock(&task->m_CritSectionIOPending);
            if (task->m_pPending[(int)dir] == NULL) {
                CObject_Unlock(&task->m_CritSectionIOPending);
                goto wait_flush;
            }
            CObject_Unlock(&task->m_CritSectionIOPending);

            msleep(5);
        }

        /* Timeout waiting for DMA */
        if (poc)
            dev_err(&poc->pdev->dev,
                    "CTask_Flush() wait for DMA complete timeout!!!\n");

            td->bFlushing[(int)data_type] = 0;
        return 0;
    }

    wait_flush:
    /* Signal task thread to process flush (bit 6 = 0x40) */
    if (task->m_Thread.m_EvtWait) {
        struct t_event_block *evt_wait = task->m_Thread.m_EvtWait;
        complete(&evt_wait->events[6]);
    }

    /* Wait for flush completion reply (bit 2, timeout 2000ms) */
    if (td->m_EvtReply) {
        ret = wait_for_completion_timeout(&td->m_EvtReply->events[2],
                                          msecs_to_jiffies(2000));
    } else {
        ret = 0;
    }

    if (ret == 0) {
        if (poc)
            dev_err(&poc->pdev->dev,
                    "CTask_Flush() failed! flushing(%d) timeout ]\n",
                    td->bFlushing[(int)data_type]);
    } else {
        if (poc)
            dev_dbg(&poc->pdev->dev,
                    "CTask_Flush() flushing(%d) ]\n",
                    td->bFlushing[(int)data_type]);
    }

    /* Determine result: success if bFlushing was cleared */
    ret = (td->bFlushing[(int)data_type] == 0) ? 1 : 0;

    /* Ensure flushing flag is cleared */
    if (td->bFlushing[(int)data_type] != 0)
        td->bFlushing[(int)data_type] = 0;

    return ret;
}

int CTask_FlushArm(struct c_task *task, u32 task_id, enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_FlushArm() hTask(%u) dataType(%d) - NEEDS IMPLEMENTING\n",
            task_id, data_type);
    return 0;
}

int CTask_DMARequest(struct c_task *task, struct task_dma_request *req)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_DMARequest() - NEEDS IMPLEMENTING\n");
    return 0;
}
/*
 * CTask_UseVideoInputDevice
 *
 * Determines if the task should use the video input device based on
 * firmware version, codec function, and video input configuration.
 *
 * Returns: 1 if video input should be used, 0 otherwise.
 */
int CTask_UseVideoInputDevice(struct c_task *task, struct task_data *td)
{

    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_UseVideoInputDevice()\n");

    /* Check Firmware API Version 0 */
    if (task->m_pMpegCodec->m_VerFwAPI == 0) {
        /* If codec function is 2 or 4, do not use video input */
        if (td->m_codec_function == 2 || td->m_codec_function == 4) {
            return 0;
        }
    }
    /* Check Firmware API Version != 0 */
    else {
        /* If bit 8 is set AND lower nibble is 0, do not use video input */
        if (((td->m_codec_function >> 8) & 1) && ((td->m_codec_function & 0xf) == 0)) {
            return 0;
        }
    }

    /* Check task validity */
    if (td->valid == 0) {
        return 0;
    }

    /* Check video input type: must be 0, 4, or 5 */
    if (td->m_video_input != 0 && td->m_video_input != 4 && td->m_video_input != 5) {
        return 0;
    }

    /* All checks passed */
    return 1;
}

int CTask_NeedVideoInputDevice(struct c_task *task)
{
    int i;
    struct task_data *td;

    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    dev_dbg(&poc->pdev->dev, "CTask_NeedVideoInputDevice()\n");

    /* Lock task object */
    CObject_Lock(&task->m_Object);

    /* Iterate through all 8 task data slots */
    for (i = 0; i < 8; i++) {
        td = &task->m_TaskData[i];

        /* Lock task data object */
        CObject_Lock(&td->m_Object);

        /* Check if task is valid, started, and requires video input */
        if (td->valid && td->m_dwStarted && CTask_UseVideoInputDevice(task, td)) {
            /* Condition met: unlock and return 1 */
            CObject_Unlock(&td->m_Object);
            CObject_Unlock(&task->m_Object);
            return 1;
        }

        /* Unlock task data object */
        CObject_Unlock(&td->m_Object);
    }

    /* No task requires video input: unlock and return 0 */
    CObject_Unlock(&task->m_Object);
    return 0;
}
