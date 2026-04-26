/* SPDX-License-Identifier: GPL-2.0 */
/*
 * queue.h - Queue structures and functions
 */

#ifndef C985_QUEUE_H
#define C985_QUEUE_H

#include "types.h"
#include "cobject.h"


/* Queue list */
struct queue_list {
    struct queue_entry *pHead;
    struct queue_entry *pTail;
};

/* CQueue structure */
struct c_queue {
    struct c_object m_Object;       /* 0x00 */
    u32 m_dwNbInQueue;              /* 0x38 */
    u8 _pad[4];                     /* 0x3C */
    struct queue_list m_Queue;      /* 0x40 */
};

/* Function prototypes */
struct c_queue *CQueue_Constructor(struct c_queue *queue, struct c_object *parent, u32 attr);
void CQueue_Destructor(struct c_queue *queue);
void CQueue_ResetQueue(struct c_queue *queue);
struct queue_entry *CQueue_GetOneEntry(struct c_queue *queue);
struct queue_entry *CQueue_GetEntryByData(struct c_queue *queue, void *data);
void CQueue_AddEntry(struct c_queue *queue, struct queue_entry *entry);
void CQueue_FlushQueue(struct c_queue *queue);


#endif /* C985_QUEUE_H */
