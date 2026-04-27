/* SPDX-License-Identifier: GPL-2.0 */
/*
 * queue.c - Queue implementation from Ghidra decompile
 */

#include <linux/spinlock.h>
#include "queue.h"
#include "cobject.h"
#include "structs.h"
#include "qposm.h"
#include "v4l2.h"
#include "pins.h"
#include "sync.h"
#include "include/abi/cqueue.h"

#define codec_to_poc(c) container_of(c, struct c985_poc, codec)


struct c_queue *CQueue_Constructor(struct c_queue *queue, struct CObject *parent, u32 attr)
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

struct QUEUE_ENTRY *CQueue_GetOneEntry(struct c_queue *queue)
{
    struct QUEUE_ENTRY *entry = NULL;
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

struct QUEUE_ENTRY *CQueue_GetEntryByData(struct c_queue *queue, void *data)
{
    struct QUEUE_ENTRY *curr;      /* ← Declared but not used! */
    struct QUEUE_ENTRY *prev = NULL;
    struct QUEUE_ENTRY *found = NULL;
    unsigned long flags = 0;

    if (!queue)
        return NULL;

    /* Lock */
    if (queue->m_Object.m_dwObjectAttributes & 1) {
        spin_lock_irqsave((spinlock_t *)&queue->m_Object.m_spinlock, flags);
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

void CQueue_AddEntry(struct c_queue *queue, struct QUEUE_ENTRY *entry)
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

struct QUEUE_ENTRY_CPP *CDataQueue_GetEntryByBufDesc(struct c_data_queue *queue,
                                                     struct _QP_BUFFER_DESCRIPTOR *buf_desc)
{
    u8 irql;
    struct QUEUE_ENTRY_CPP *cur;
    struct QUEUE_ENTRY_CPP *prev = NULL;

    if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
        if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
            KeWaitForSingleObject((char *)queue + 0x38, 0, 0, 0, 0);
        }
    }
    else {
        irql = KeAcquireSpinLockRaiseToDpc((char *)queue + 0x28);
        *(u8 *)((char *)queue + 0x30) = irql;
    }

    cur = *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x70);
    prev = NULL;

    do {
        if (cur == NULL) {
            if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
                if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
                    KeReleaseMutex((char *)queue + 0x38, 0);
                }
            }
            else {
                KeReleaseSpinLock((char *)queue + 0x28, *(u8 *)((char *)queue + 0x30));
            }
            return cur;
        }

        if (cur->Data->pBufDesc == buf_desc) {
            if (prev == NULL) {
                *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x70) = cur->pNext;

                if (*(u64 *)((char *)queue + 0x70) == 0) {
                    *(u64 *)((char *)queue + 0x78) = *(u64 *)((char *)queue + 0x70);
                }
            }
            else {
                prev->pNext = cur->pNext;

                if (cur->pNext == NULL) {
                    *(struct QUEUE_ENTRY_CPP **)((char *)queue + 0x78) = prev;
                }
            }

            *(int *)((char *)queue + 0x80) = *(int *)((char *)queue + 0x80) - 1;

            if ((*(u32 *)((char *)queue + 0x18) & 1) == 0) {
                if ((*(u32 *)((char *)queue + 0x18) & 2) != 0) {
                    KeReleaseMutex((char *)queue + 0x38, 0);
                }
            }
            else {
                KeReleaseSpinLock((char *)queue + 0x28, *(u8 *)((char *)queue + 0x30));
            }
            return NULL;
        }

        prev = cur;
        cur = cur->pNext;
    } while (1);
}
EXPORT_SYMBOL_GPL(CDataQueue_GetEntryByBufDesc);
struct CppObject *CppObject_CppObject(struct CppObject *this,
                                      struct CppObject *parent,
                                      u32 who_am_i,
                                      u32 object_attributes)
{
    if (!this)
        return NULL;

    this->_padding_ = 0;
    this->m_pParent = parent;
    this->m_fInitialized = 0;
    this->m_dwWhoAmI = who_am_i;
    this->m_dwObjectAttributes = object_attributes;

    if ((this->m_dwObjectAttributes & 1) != 0) {
        spin_lock_init(&this->m_spinlock);
    } else if ((this->m_dwObjectAttributes & 2) != 0) {
        mutex_init(&this->m_mutex);
    }

    return this;
}

struct cpp_queue *CppQueue_CppQueue(struct cpp_queue *this,
                                    struct CppObject *parent,
                                    u32 who_am_i,
                                    u32 object_attributes,
                                    _EQPErrors (*error_handler)(void *))
{
    if (!this)
        return NULL;

    CppObject_CppObject(&this->m_Object, parent, who_am_i, object_attributes);

    this->m_dwNbInQueue = 0;
    this->m_pFuncCallback = error_handler;

    CppQueue_ResetQueue(this);

    return this;
}
EXPORT_SYMBOL_GPL(CppQueue_CppQueue);

struct c_data_queue *CDataQueue_CDataQueue(struct c_data_queue *this,
                                           struct CppObject *parent,
                                           u32 who_am_i,
                                           u32 object_attributes,
                                           _EQPErrors (*error_handler)(void *))
{
    if (!this)
        return NULL;

    CppQueue_CppQueue(&this->base, parent, who_am_i, object_attributes, error_handler);

    return this;
}

void CQueue_ResetQueue(struct c_queue *param_1)
{
    if (!param_1)
        return;

    param_1->m_Queue.pTail = NULL;
    param_1->m_Queue.pHead = NULL;
    param_1->m_dwNbInQueue = 0;
}

void CppQueue_ResetQueue(struct cpp_queue *this)
{
    unsigned long flags = 0;

    if (!this)
        return;

    if ((this->m_Object.m_dwObjectAttributes & 1) != 0) {
        spin_lock_irqsave(&this->m_Object.m_spinlock, flags);
    } else if ((this->m_Object.m_dwObjectAttributes & 2) != 0) {
        mutex_lock(&this->m_Object.m_mutex);
    }

    this->m_Queue.pTail = NULL;
    this->m_Queue.pHead = NULL;
    this->m_dwNbInQueue = 0;

    if ((this->m_Object.m_dwObjectAttributes & 1) != 0) {
        spin_unlock_irqrestore(&this->m_Object.m_spinlock, flags);
    } else if ((this->m_Object.m_dwObjectAttributes & 2) != 0) {
        mutex_unlock(&this->m_Object.m_mutex);
    }
}
