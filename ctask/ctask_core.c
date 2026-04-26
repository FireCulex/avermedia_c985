// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_core.c - CTask constructor, destructor, initialization
 */

#include "ctask_private.h"

/* ================================================================
 * CTask_Constructor
 * ================================================================ */
struct c_task *CTask_Constructor(struct c_task *task, struct c_object *parent,
                                 u32 param_3, u32 param_4, struct cql_codec *codec,
                                 void *evt_wait, void *evt_reply)
{
    struct c985_poc *poc;
    struct c_thread *thread;
    int ret;

    if (!task || !codec)
        return NULL;

    poc = codec_to_poc(codec);
    dev_dbg(&poc->pdev->dev, "CTask_Constructor()\n");

    CObject_Constructor(&task->m_Object, parent, param_3);

    thread = CThread_Constructor(&task->m_Thread, "CTask_ThreadProc", task,
                                 param_4, evt_wait, evt_reply, 1);
    if (!thread)
        return NULL;

    /* Set thread proc */
    task->m_Thread.m_ThreadProc = CTask_ThreadProc;

    /* Set function pointers */
    task->Alloc = CTask_Alloc;
    task->Release = CTask_Release;
    task->Open = CTask_Open;
    task->Close = CTask_Close;
    task->Start = CTask_Start;
    task->Stop = CTask_Stop;
    task->Acquire = CTask_Acquire;
    task->Pause = CTask_Pause;
    task->Resume = CTask_Resume;
    task->CancelBuffer = CTask_CancelBuffer;
    task->NewBuffer = CTask_NewBuffer;
    task->Flush = CTask_Flush;
    task->FlushArm = CTask_FlushArm;
    task->DMARequest = CTask_DMARequest;
    task->CompleteData = CTask_CompleteData;
    task->CompleteUser = CTask_CompleteUser;
    task->CompleteArm = CTask_CompleteArm;
    task->ProcessArmMessage = CTask_ProcessArmMessage;
    task->ProcessIoComplete = CTask_ProcessIoComplete;
    task->ProcessDataStreaming = CTask_ProcessDataStreaming;
    task->ProcessCancelBuffer = CTask_ProcessCancelBuffer;
    task->ProcessFlush = CTask_ProcessFlush;
    task->ProcessFlushArm = CTask_ProcessFlushArm;

    /* Set codec pointer */
    task->m_pMpegCodec = codec;

    /* Initialize critical section for IO pending */
    CObject_Constructor(&task->m_CritSectionIOPending, &task->m_Object, 1);

    /* Initialize tasks */
    ret = CTask_InitTasks(task);
    if (ret == 0) {
        CTask_Destructor(task);
        return NULL;
    }

    return task;
}

/* ================================================================
 * CTask_Destructor
 * ================================================================ */
void CTask_Destructor(struct c_task *task)
{
    struct c985_poc *poc;

    if (!task)
        return;

    if (task->m_pMpegCodec) {
        poc = codec_to_poc(task->m_pMpegCodec);
        dev_dbg(&poc->pdev->dev, "CTask_Destructor()\n");
    }

    /* Clean up tasks */
    CTask_DoneTasks(task);

    /* Stop and destroy kernel thread */
    if (task->m_Thread.m_threadID &&
        !IS_ERR((struct task_struct *)task->m_Thread.m_threadID)) {
        kthread_stop((struct task_struct *)task->m_Thread.m_threadID);
    task->m_Thread.m_threadID = NULL;
        }
}

/* ================================================================
 * CTask_InitTasks
 * ================================================================ */
int CTask_InitTasks(struct c_task *task)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    u32 i;

    dev_dbg(&poc->pdev->dev, "CTask_InitTasks()\n");

    /* Initialize DMA request complete event - embedded, not allocated */
    task->m_EvtDmaReqComplete = kzalloc(sizeof(struct t_event_block), GFP_KERNEL);
    if (task->m_EvtDmaReqComplete)
        QPOSMCreateEvtgrp(task->m_EvtDmaReqComplete);

    /* Initialize all 8 task data slots */
    for (i = 0; i < MAX_TASKS; i++) {
        memset(&task->m_TaskData[i], 0, sizeof(struct task_data));
        task->m_TaskData[i].id = i;
        task->m_TaskData[i].valid = 0;
        task->m_TaskData[i].m_State = TASK_STATE_IDLE;

        /* Initialize per-task object */
        CObject_Constructor(&task->m_TaskData[i].m_Object, &task->m_Object, 2);

        /* Initialize per-task reply event - embedded */
        task->m_TaskData[i].m_EvtReply = kzalloc(sizeof(struct t_event_block), GFP_KERNEL);
        if (task->m_TaskData[i].m_EvtReply)
            QPOSMCreateEvtgrp(task->m_TaskData[i].m_EvtReply);
    }

    /* Initialize IO pending structures */
    for (i = 0; i < 2; i++) {
        memset(&task->m_ioPending[i], 0, sizeof(struct task_io_pending));
        task->m_pPending[i] = NULL;
    }

    /* Initialize scheduling */
    task->m_dwMaxDMASize = 0xFFFFFFFF;
    task->m_StartID = 0;
    task->m_taskIdPCMOut = 0xFFFFFFFF;
    task->m_taskIdAESOut = 0xFFFFFFFF;

    /* Initialize DMA request */
    memset(&task->m_dmaRequest, 0, sizeof(struct task_dma_request));
    task->m_pDmaRequest = NULL;

    return 1;
}

/* ================================================================
 * CTask_DoneTasks
 * ================================================================ */
void CTask_DoneTasks(struct c_task *task)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    u32 i;

    dev_dbg(&poc->pdev->dev, "CTask_DoneTasks()\n");

    for (i = 0; i < MAX_TASKS; i++) {
        if (task->Release)
            ((int (*)(struct c_task *, u32))task->Release)(task, i);

        /* Free the allocated event block */
        if (task->m_TaskData[i].m_EvtReply) {
            QPOSMDeleteEvtgrp(task->m_TaskData[i].m_EvtReply);
            task->m_TaskData[i].m_EvtReply = NULL;
        }
    }

    /* Free DMA event block */
    if (task->m_EvtDmaReqComplete) {
        QPOSMDeleteEvtgrp(task->m_EvtDmaReqComplete);
        task->m_EvtDmaReqComplete = NULL;
    }
}
/* ================================================================
 * CTask_GetTaskState
 * ================================================================ */
enum task_state CTask_GetTaskState(struct c_task *task, u32 task_id)
{
    if (task_id >= MAX_TASKS)
        return TASK_STATE_IDLE;

    if (task->m_TaskData[task_id].valid == 0)
        return TASK_STATE_IDLE;

    return task->m_TaskData[task_id].m_State;
}
