/* SPDX-License-Identifier: GPL-2.0 */
/*
 * queue.c - Queue implementation from Ghidra decompile
 */

#include <linux/spinlock.h>
#include "queue.h"
#include "cobject.h"
#include "structs.h"

#define codec_to_poc(c) container_of(c, struct c985_poc, codec)


void CQueue_ResetQueue(struct c_queue *queue)
{
    if (!queue)
        return;

    queue->m_Queue.pHead = NULL;
    queue->m_Queue.pTail = NULL;
    queue->m_dwNbInQueue = 0;
}

struct c_queue *CQueue_Constructor(struct c_queue *queue, struct c_object *parent, u32 attr)
{
    if (!queue)
        return NULL;

    CObject_Constructor(&queue->m_Object, parent, attr);
    queue->m_dwNbInQueue = 0;
    CQueue_ResetQueue(queue);

    return queue;
}

void CQueue_Destructor(struct c_queue *queue)
{
    if (!queue)
        return;

    CQueue_ResetQueue(queue);
    CObject_Destructor(&queue->m_Object);
}

struct queue_entry *CQueue_GetOneEntry(struct c_queue *queue)
{
    struct queue_entry *entry = NULL;
    unsigned long flags = 0;

    if (!queue)
        return NULL;

    if (queue->m_Object.m_pParent) {
        struct c_channel *ch = container_of(queue->m_Object.m_pParent, struct c_channel, m_Object);
        if (ch && ch->m_pTask && ch->m_pTask->m_pMpegCodec) {
            struct c985_poc *poc = codec_to_poc(ch->m_pTask->m_pMpegCodec);
            dev_info(&poc->pdev->dev,
                     "CQueue_GetOneEntry: queue=%px channel_type=%d direction=%d nbInQueue=%d\n",
                     queue, ch->m_ChannelType, ch->m_ChannelDirection, queue->m_dwNbInQueue);
        }
    }

    /* Lock based on object attributes */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        /* Spinlock mode */
        spin_lock_irqsave((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        queue->m_Object.m_irql = (u8)flags;
    } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
        queue->m_Object.m_semCriticalSection) {
        /* Semaphore mode - use mutex in Linux */
        mutex_lock((struct mutex *)queue->m_Object.m_semCriticalSection);
        }

        /* Get entry from head */
        if (queue->m_Queue.pHead) {
            entry = queue->m_Queue.pHead;
            queue->m_Queue.pHead = entry->pNext;
            if (!queue->m_Queue.pHead) {
                queue->m_Queue.pTail = NULL;
            }
            queue->m_dwNbInQueue--;
        }

        /* Unlock */
        if (queue->m_Object.m_dwObjectAttributes & 1) {
            spin_unlock_irqrestore((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
            queue->m_Object.m_semCriticalSection) {
            mutex_unlock((struct mutex *)queue->m_Object.m_semCriticalSection);
            }

            return entry;
}

struct queue_entry *CQueue_GetEntryByData(struct c_queue *queue, void *data)
{
    struct queue_entry *curr;      /* ← Declared but not used! */
    struct queue_entry *prev = NULL;
    struct queue_entry *found = NULL;
    unsigned long flags = 0;

    if (!queue)
        return NULL;

    /* Lock */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        spin_lock_irqsave((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        queue->m_Object.m_irql = (u8)flags;
    } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
        queue->m_Object.m_semCriticalSection) {
        mutex_lock((struct mutex *)queue->m_Object.m_semCriticalSection);
        }

        /* Search for entry - CHANGE ALL 'current' TO 'curr' */
        curr = queue->m_Queue.pHead;           /* ← Was: current = */
        while (curr) {                         /* ← Was: current */
            if (curr->Data == data) {          /* ← Was: current->Data */
                /* Found - remove from list */
                if (!prev) {
                    /* Head of list */
                    queue->m_Queue.pHead = curr->pNext;      /* ← Was: current->pNext */
                    if (!queue->m_Queue.pHead) {
                        queue->m_Queue.pTail = NULL;
                    }
                } else {
                    prev->pNext = curr->pNext;               /* ← Was: current->pNext */
                    if (!curr->pNext) {                      /* ← Was: current->pNext */
                        queue->m_Queue.pTail = prev;
                    }
                }
                queue->m_dwNbInQueue--;
                found = curr;                                /* ← Was: current */
                break;
            }
            prev = curr;                                     /* ← Was: current */
            curr = curr->pNext;                              /* ← Was: current->pNext */
        }

        /* Unlock */
        if (queue->m_Object.m_dwObjectAttributes & 1) {
            spin_unlock_irqrestore((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
            queue->m_Object.m_semCriticalSection) {
            mutex_unlock((struct mutex *)queue->m_Object.m_semCriticalSection);
            }

            return found;
}

void CQueue_AddEntry(struct c_queue *queue, struct queue_entry *entry)
{
    unsigned long flags = 0;
    struct c985_poc *poc = NULL;

    if (!queue || !entry)
        return;

    if (queue->m_Object.m_pParent) {
        struct c_channel *ch = container_of(queue->m_Object.m_pParent, struct c_channel, m_Object);
        if (ch && ch->m_pTask && ch->m_pTask->m_pMpegCodec) {
            poc = codec_to_poc(ch->m_pTask->m_pMpegCodec);
            dev_info(&poc->pdev->dev,
                     "CQueue_AddEntry: queue=%px channel_type=%d direction=%d data=%px\n",
                     queue, ch->m_ChannelType, ch->m_ChannelDirection, entry->Data);
        }
    }

    /* Lock */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        spin_lock_irqsave((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        queue->m_Object.m_irql = (u8)flags;
    } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
        queue->m_Object.m_semCriticalSection) {
        mutex_lock((struct mutex *)queue->m_Object.m_semCriticalSection);
        }

        /* Add to tail */
        if (!queue->m_Queue.pTail) {
            queue->m_Queue.pHead = entry;
            queue->m_Queue.pTail = entry;
        } else {
            queue->m_Queue.pTail->pNext = entry;
            queue->m_Queue.pTail = entry;
        }
        entry->pNext = NULL;
        queue->m_dwNbInQueue++;

        /* Unlock */
        if (queue->m_Object.m_dwObjectAttributes & 1) {
            spin_unlock_irqrestore((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
            queue->m_Object.m_semCriticalSection) {
            mutex_unlock((struct mutex *)queue->m_Object.m_semCriticalSection);
            }
}
void CQueue_FlushQueue(struct c_queue *queue)
{
    unsigned long flags = 0;

    if (!queue)
        return;

    /* Lock */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        spin_lock_irqsave((spinlock_t *)&queue->m_Object.m_spinlock, flags);
        queue->m_Object.m_irql = (u8)flags;
    } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
        queue->m_Object.m_semCriticalSection) {
        mutex_lock((struct mutex *)queue->m_Object.m_semCriticalSection);
        }

        /* Drain all entries from the queue */
        while (queue->m_Queue.pHead) {
            queue->m_Queue.pHead = queue->m_Queue.pHead->pNext;
            queue->m_dwNbInQueue--;
        }
        queue->m_Queue.pTail = NULL;

    /* Unlock */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        spin_unlock_irqrestore((spinlock_t *)&queue->m_Object.m_spinlock, flags);
    } else if ((queue->m_Object.m_dwObjectAttributes & 2) &&
        queue->m_Object.m_semCriticalSection) {
        mutex_unlock((struct mutex *)queue->m_Object.m_semCriticalSection);
        }
}
