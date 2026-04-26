// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_alloc.c - Task allocation and release
 */

#include "ctask_private.h"

/* ================================================================
 * CTask_Alloc
 * ================================================================ */
int CTask_Alloc(struct c_task *task, enum task_type type, u32 *task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct task_data *td = NULL;
    u32 i;
    int ret;

    /* Initialize max DMA size if not set */
    if (task->m_dwMaxDMASize == 0xFFFFFFFF) {
        task->m_dwMaxDMASize = QPHCI_GetMaxDMASize(&task->m_pMpegCodec->m_hci);
    }

    *task_id = 0xFFFFFFFF;

    CObject_lock(&task->m_Object);

    /* Search for free slot */
    for (i = 0; i < MAX_TASKS; i++) {
        if (task->m_TaskData[i].valid == 0) {
            td = &task->m_TaskData[i];
            td->type = type;

            CTask_LoadDefaultSettings(task, td);

            td->m_dwSession = 0;

            /* Clear channel pointers */
            memset(td->pChannel, 0, sizeof(td->pChannel));

            CTask_ResetState(task, td);

            *task_id = i;
            td->valid = 1;

            break;
        }
    }

    CObject_unlock(&task->m_Object);

    dev_dbg(&poc->pdev->dev, "CTask_Alloc() handle(%d)\n", *task_id);

    if (!td)
        return -ENOMEM;

    return 0;
}

/* ================================================================
 * CTask_Release
 * ================================================================ */
int CTask_Release(struct c_task *task, u32 task_id)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    enum task_data_type dtype;

    dev_dbg(&poc->pdev->dev, "CTask_Release() (%d)\n", task_id);

    if (task_id >= MAX_TASKS)
        return -EINVAL;

    CObject_lock(&task->m_Object);

    if (task->m_TaskData[task_id].valid != 0) {
        /* Close all data types */
        for (dtype = TASK_DATA_TYPE_COMP_VID; (int)dtype < TASK_DATA_TYPE_MAX; dtype++) {
            if (task->Close)
                ((int (*)(struct c_task *, u32, enum task_data_type, int))
                task->Close)(task, task_id, dtype, 0);
        }

        task->m_TaskData[task_id].valid = 0;
    }

    /* Clear special task IDs if this was one */
    if (task_id == task->m_taskIdPCMOut) {
        task->m_taskIdPCMOut = 0xFFFFFFFF;
    } else if (task_id == task->m_taskIdAESOut) {
        task->m_taskIdAESOut = 0xFFFFFFFF;
    }

    CObject_unlock(&task->m_Object);

    return 0;
}
