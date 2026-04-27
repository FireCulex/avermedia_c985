/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ctask_private.h - Internal helpers for CTask
 */
#ifndef CTASK_PRIVATE_H
#define CTASK_PRIVATE_H

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "ctask.h"
#include "../structs.h"
#include "../types.h"
#include "../include/abi/qp_buffer_descriptor.h"
#include "../avermedia_c985.h"
#include "../cobject.h"
#include "../qpfwapi.h"
#include "../qpfwencapi.h"
#include "../qphci.h"
#include "../dma.h"
#include "../interrupts.h"

/* ============================================
 * Sync Mode Table
 * ============================================ */
static const u32 g_syncModeTable[] = {
    0, 0, 1, 1, 2, 2, 3, 3
};

/* ============================================
 * Get c985_poc from cql_codec
 * ============================================ */
static inline struct c985_poc *codec_to_poc(struct cql_codec *codec)
{
    return container_of(codec, struct c985_poc, codec);
}

/* ============================================
 * CObject Critical Section Helpers
 * ============================================ */
static inline void CObject_enterCritical(struct CObject *obj)
{
    if (obj && obj->m_semCriticalSection)
        mutex_lock((struct mutex *)obj->m_semCriticalSection);
}

static inline void CObject_leaveCritical(struct CObject *obj)
{
    if (obj && obj->m_semCriticalSection)
        mutex_unlock((struct mutex *)obj->m_semCriticalSection);
}

static inline void CObject_lock(struct CObject *obj)
{
    if (!obj)
        return;

    if ((obj->m_dwObjectAttributes & 1) == 0) {
        if (((obj->m_dwObjectAttributes & 2) != 0) &&
            (obj->m_semCriticalSection != NULL)) {
            mutex_lock((struct mutex *)obj->m_semCriticalSection);
            }
    } else {
        if (obj->m_semCriticalSection)
            mutex_lock((struct mutex *)obj->m_semCriticalSection);
    }
}

static inline void CObject_unlock(struct CObject *obj)
{
    if (!obj)
        return;

    if ((obj->m_dwObjectAttributes & 1) == 0) {
        if (((obj->m_dwObjectAttributes & 2) != 0) &&
            (obj->m_semCriticalSection != NULL)) {
            mutex_unlock((struct mutex *)obj->m_semCriticalSection);
            }
    } else {
        if (obj->m_semCriticalSection)
            mutex_unlock((struct mutex *)obj->m_semCriticalSection);
    }
}

/* ============================================
 * Event Group Helpers (Linux wait queue wrappers)
 * ============================================ */
static inline void qposm_set_event(wait_queue_head_t *wq, u32 *events, u32 mask)
{
    if (wq && events) {
        *events |= mask;
        wake_up_interruptible(wq);
    }
}

static inline int qposm_wait_event(wait_queue_head_t *wq, u32 *events,
                                   u32 mask, u32 *out_events, int timeout_ms)
{
    int ret;

    if (!wq || !events)
        return -EINVAL;

    if (timeout_ms < 0) {
        ret = wait_event_interruptible(*wq, (*events & mask));
    } else {
        ret = wait_event_interruptible_timeout(*wq, (*events & mask),
                                               msecs_to_jiffies(timeout_ms));
        if (ret == 0)
            return -ETIMEDOUT;
    }

    if (ret < 0)
        return ret;

    if (out_events)
        *out_events = *events & mask;
    *events &= ~mask;

    return 0;
}

/* ============================================
 * Mutex Helpers
 * ============================================ */
static inline int QPOSMLockMutex(void *mutex_ptr)
{
    if (mutex_ptr) {
        mutex_lock((struct mutex *)mutex_ptr);
        return 1;
    }
    return 0;
}

static inline void QPOSMUnlockMutex(void *mutex_ptr)
{
    if (mutex_ptr)
        mutex_unlock((struct mutex *)mutex_ptr);
}

/* ============================================
 * Event Group API (Windows compatibility layer)
 * ============================================ */
int QPOSMSetEvtgrp(void *evtgrp, u32 mask);
int QPOSMWaitEvtgrp(void *evtgrp, u32 mask, u32 *out_events, long timeout);
void QPOSMCreateEvtgrp(struct t_event_block *evt);
int QPOSMClearEvtgrp(void *evtgrp, u32 mask);
int QPOSMDeleteEvtgrp(void *param_1);
void QPOSMDeleteMutex(void *mutex);
int QPOSMDeleteSem(void *param_1);

/* ============================================
 * DMA Wrappers
 * ============================================ */
static inline int CQLCodec_StartDMARead_Full(struct cql_codec *codec, u32 arm_addr,
                                             u8 *host_buf, u32 length, int swap,
                                             int unused1, u32 xfer_mode,
                                             u32 pic_width, u32 pic_height, int unused2)
{
    struct c985_poc *poc = codec_to_poc(codec);
    u32 card_addr = arm_addr << 2;
    return CQLCodec_StartDMARead(poc, host_buf, card_addr, length);
}

static inline int CQLCodec_StartDMAWrite_Full(struct cql_codec *codec, u32 arm_addr,
                                              u8 *host_buf, u32 length, int swap,
                                              int unused1, u32 xfer_mode,
                                              u32 pic_width, u32 pic_height, int unused2)
{
    struct c985_poc *poc = codec_to_poc(codec);
    u32 card_addr = arm_addr << 2;
    return CQLCodec_StartDMAWrite(poc, host_buf, card_addr, length);
}

#endif /* CTASK_PRIVATE_H */
