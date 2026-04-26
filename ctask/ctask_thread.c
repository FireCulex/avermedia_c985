/* SPDX-License-Identifier: GPL-2.0 */
#include "ctask_private.h"

/* Stub for now - implement later from Ghidra */
int ProjectC985E_checkDevice(void *param_1)
{
    /* For now, just return success */
    return QPERR_SUCCESS;
}

/* ================================================================
 * CThread_Constructor
 * ================================================================ */
struct c_thread *CThread_Constructor(struct c_thread *thread, const char *name,
                                     void *context, u32 priority,
                                     void *evt_wait, void *evt_reply, int flag)
{
    if (!thread)
        return NULL;

    memset(thread, 0, sizeof(*thread));

    thread->m_context = context;
    thread->m_priority = priority;
    thread->m_EvtWait = evt_wait;
    thread->m_EvtReply = evt_reply;
    thread->m_ThreadProc = NULL;
    thread->m_threadID = NULL;

    if (name)
        strncpy(thread->m_szThreadName, name, sizeof(thread->m_szThreadName) - 1);

    return thread;
}

/* ================================================================
 * CThread_ThreadInit
 * ================================================================ */
int CThread_ThreadInit(struct c_thread *thread)
{
    struct task_struct *task;

    if (!thread || !thread->m_ThreadProc)
        return 0;

    task = kthread_run((int (*)(void *))thread->m_ThreadProc,
                       thread, "%s", thread->m_szThreadName);
    if (IS_ERR(task))
        return 0;

    thread->m_threadID = task;
    return 1;
}

/* ================================================================
 * CThread_ThreadDone
 * ================================================================ */
void CThread_ThreadDone(struct c_thread *thread)
{
    if (!thread)
        return;

    if (thread->m_threadID && !IS_ERR((struct task_struct *)thread->m_threadID)) {
        /* Signal thread to exit */
        if (thread->m_EvtWait)
            QPOSMSetEvtgrp(thread->m_EvtWait, THREAD_EVENT_EXIT);

        kthread_stop((struct task_struct *)thread->m_threadID);
        thread->m_threadID = NULL;
    }
}

/* ================================================================
 * CTask_ThreadProc
 * ================================================================ */
/* Event bit definitions - matching Windows driver */
#define THREAD_EVENT_EXIT           0x001
#define THREAD_EVENT_PROCESS_ARM    0x004  /* ProcessArmMessage */
#define THREAD_EVENT_DMA_READ       0x008  /* CompleteData(READ) */
#define THREAD_EVENT_DMA_WRITE      0x010  /* CompleteData(WRITE) */
#define THREAD_EVENT_PROCESS_DATA   0x020  /* ProcessDataStreaming */
#define THREAD_EVENT_COMPLETE_USER  0x040  /* CompleteUser loop */
#define THREAD_EVENT_HANDLER_80     0x080  /* Handler + reply 0x4 */
#define THREAD_EVENT_HANDLER_100    0x100  /* Handler + reply 0x8 */

#define THREAD_EVENT_ALL            0x1FF

/* Reply events */
#define REPLY_EVENT_EXIT            0x001
#define REPLY_EVENT_COMPLETE_USER   0x002
#define REPLY_EVENT_ARM_MSG         0x004
#define REPLY_EVENT_HANDLER_100     0x008

/* ================================================================
 * CTask_ThreadProc
 * ================================================================ */
int CTask_ThreadProc(void *data)
{
    struct c_thread *thread = (struct c_thread *)data;
    struct c_task *task;
    struct c985_poc *poc;
    u32 events = 0;
    int ret;
    int i, j;

    if (!thread || !thread->m_context)
        return -EINVAL;

    task = (struct c_task *)thread->m_context;
    poc = codec_to_poc(task->m_pMpegCodec);

    dev_dbg(&poc->pdev->dev, "CTask_ThreadProc() ENTER\n");

    do {
        ret = QPOSMWaitEvtgrp(thread->m_EvtWait, THREAD_EVENT_ALL, &events, -1);

        dev_dbg(&poc->pdev->dev,
                "CTask_ThreadProc() woke up events(0x%x) status(%d)\n",
                events, ret);

        if (ret < 0) {
            dev_err(&poc->pdev->dev,
                    "CTask_ThreadProc() QPOSMWaitEvtgrp (%d) failed!!!\n", ret);
            break;
        }

        /* 0x04 - Process ARM message */
        if (events & THREAD_EVENT_ARM_MSG) {
            if (task->ProcessArmMessage)
                ((void (*)(struct c_task *))task->ProcessArmMessage)(task);
        }

        /* 0x08 - CompleteData for READ direction */
        if (events & THREAD_EVENT_DMA_READ) {
            if (task->CompleteData)
                ((void (*)(struct c_task *, enum channel_direction))
                task->CompleteData)(task, CHANNEL_DIR_READ);
        }

        /* 0x10 - CompleteData for WRITE direction */
        if (events & THREAD_EVENT_DMA_WRITE) {
            if (task->CompleteData)
                ((void (*)(struct c_task *, enum channel_direction))
                task->CompleteData)(task, CHANNEL_DIR_WRITE);
        }

        /* 0x20 - Process data streaming */
        if (events & THREAD_EVENT_PROCESS_DATA) {
            if (task->ProcessDataStreaming)
                ((void (*)(struct c_task *))task->ProcessDataStreaming)(task);
        }

        /* 0x40 - Process cancel buffer / CompleteUser loop */
        if (events & THREAD_EVENT_CANCEL_BUFFER) {
            for (i = 0; i < 8; i++) {
                if (task->m_TaskData[i].m_State != TASK_STATE_IDLE) {
                    for (j = 0; j < 7; j++) {
                        if (task->m_TaskData[i].bFlushing[j] != 0) {
                            if (task->CompleteUser)
                                ((int (*)(struct c_task *, struct task_data *, enum task_data_type))
                                task->CompleteUser)(task, &task->m_TaskData[i], (enum task_data_type)j);
                            QPOSMSetEvtgrp(&task->m_TaskData[i].m_EvtReply, 0x02);
                        }
                    }
                }
            }
        }

        /* 0x80 - Process flush */
        if (events & THREAD_EVENT_FLUSH) {
            if (task->ProcessFlush)
                ((void (*)(struct c_task *))task->ProcessFlush)(task);
            QPOSMSetEvtgrp(thread->m_EvtReply, REPLY_EVENT_ARM_MSG);
        }

        /* 0x100 - Process flush ARM */
        if (events & THREAD_EVENT_FLUSH_ARM) {
            if (task->ProcessFlushArm)
                ((void (*)(struct c_task *))task->ProcessFlushArm)(task);
            QPOSMSetEvtgrp(thread->m_EvtReply, REPLY_EVENT_UNKNOWN_08);
        }

    } while ((events & THREAD_EVENT_EXIT) == 0);

    dev_dbg(&poc->pdev->dev, "CTask_ThreadProc() THREAD_EVENT_EXIT\n");
    QPOSMSetEvtgrp(thread->m_EvtReply, REPLY_EVENT_EXIT);

    dev_dbg(&poc->pdev->dev, "CTask_ThreadProc() See Ya!\n");
    return 0;
}


