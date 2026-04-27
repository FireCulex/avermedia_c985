/* SPDX-License-Identifier: GPL-2.0 */
/*
 * queue.h - Queue structures and functions
 */

#ifndef C985_QUEUE_H
#define C985_QUEUE_H

#include "types.h"
#include "cobject.h"
#include "pins.h"
#include "include/abi/cobject.h"
#include "include/abi/cppobject.h"

/* Queue list */
struct queue_list {
    struct QUEUE_ENTRY *pHead;
    struct QUEUE_ENTRY *pTail;
};


/* Function prototypes */
struct c_queue *CQueue_Constructor(struct c_queue *queue, struct CObject *parent, u32 attr);
void CQueue_Destructor(struct c_queue *queue);
void CQueue_ResetQueue(struct c_queue *queue);
struct QUEUE_ENTRY *CQueue_GetOneEntry(struct c_queue *queue);
struct QUEUE_ENTRY *CQueue_GetEntryByData(struct c_queue *queue, void *data);
void CQueue_AddEntry(struct c_queue *queue, struct QUEUE_ENTRY *entry);
void CQueue_FlushQueue(struct c_queue *queue);
struct QUEUE_ENTRY_CPP *CDataQueue_GetEntryByBufDesc(struct c_data_queue *queue,
                                                      struct _QP_BUFFER_DESCRIPTOR *desc);
/* CppObject constructor */
struct CppObject *CppObject_CppObject(struct CppObject *this,
                                      struct CppObject *parent,
                                      u32 who_am_i,
                                      u32 object_attributes);

/* CppQueue constructor */
struct c_queue *CppQueue_CppQueue(struct c_queue *this,
                                  struct CppObject *parent,
                                  u32 who_am_i,
                                  u32 object_attributes,
                                  _EQPErrors (*error_handler)(void *));

/* CDataQueue constructor */
struct c_data_queue *CDataQueue_CDataQueue(struct c_data_queue *this,
                                           struct CppObject *parent,
                                           u32 who_am_i,
                                           u32 object_attributes,
                                           _EQPErrors (*error_handler)(void *));


#endif /* C985_QUEUE_H */
