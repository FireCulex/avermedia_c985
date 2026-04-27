#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include "qposm.h"
#include "qperrors.h"
#include "structs.h"

static atomic_t s_alloc_count = ATOMIC_INIT(0);

void *QPOSMMalloc(unsigned long size)
{
    void *ptr;

    ptr = kzalloc(size, GFP_KERNEL);

    if (ptr) {
        atomic_inc(&s_alloc_count);
        pr_debug("QPOSMMalloc() ptr(%p) size(%lu) cnt(%d)\n",
                 ptr, size, atomic_read(&s_alloc_count));
    } else {
        pr_debug("QPOSMMalloc() FAILED size(%lu)\n", size);
    }

    return ptr;
}

void QPOSMFree(void *ptr)
{
    if (ptr) {
        atomic_dec(&s_alloc_count);
        pr_debug("QPOSMFree() ptr(%p) cnt(%d)\n",
                 ptr, atomic_read(&s_alloc_count));
        kfree(ptr);
    }
}

int QPOSMGetAllocCount(void)
{
    return atomic_read(&s_alloc_count);
}

void QPOSMCreateEvtgrp(struct t_event_block *evt)
{
    int i;

    evt->check = 0x69677665;
    evt->bits = 0;
    evt->mutexID = NULL;
    spin_lock_init((spinlock_t *)&evt->spinlock);

    for (i = 0; i < 32; i++) {
        init_completion(&evt->events[i]);
    }
}

int QPOSMSetEvtgrp(void *evtgrp, u32 mask)
{
    struct t_event_block *evt;
    unsigned long flags;
    int i;

    pr_debug("QPOSMSetEvtgrp: entered 0x%pK, 0x%X\n", evtgrp, mask);

    if (!evtgrp) {
        pr_debug("QPOSMSetEvtgrp: NULL event group ID\n");
        return QPERR_PARMS;
    }

    evt = (struct t_event_block *)evtgrp;

    if (evt->check == 0x64677665) {
        pr_debug("QPOSMSetEvtgrp: event deleted\n");
        return QPERR_PARMS;
    }

    if (evt->check != 0x69677665 && evt->check != 0x77677665) {
        pr_debug("QPOSMSetEvtgrp: unknown event state 0x%X\n", evt->check);
        return QPERR_PARMS;
    }

    spin_lock_irqsave((spinlock_t *)&evt->spinlock, flags);

    for (i = 0; i < 32; i++) {
        if ((mask & (1 << i)) && !(evt->bits & (1 << i))) {
            evt->bits |= (1 << i);
            complete(&evt->events[i]);
        }
    }

    spin_unlock_irqrestore((spinlock_t *)&evt->spinlock, flags);

    pr_debug("QPOSMSetEvtgrp: exiting 0\n");
    return QPERR_SUCCESS;
}

int QPOSMClearEvtgrp(void *evtgrp, u32 mask)
{
    struct t_event_block *evt;
    unsigned long flags;
    int i;

    pr_debug("QPOSMClearEvtgrp: entered 0x%pK, 0x%X\n", evtgrp, mask);

    if (!evtgrp) {
        pr_debug("QPOSMClearEvtgrp: NULL event group ID\n");
        return QPERR_PARMS;
    }

    evt = (struct t_event_block *)evtgrp;

    if (evt->check == 0x64677665) {
        pr_debug("QPOSMClearEvtgrp: event deleted\n");
        return QPERR_PARMS;
    }

    if (evt->check != 0x69677665 && evt->check != 0x77677665) {
        pr_debug("QPOSMClearEvtgrp: unknown event state 0x%X\n", evt->check);
        return QPERR_PARMS;
    }

    spin_lock_irqsave((spinlock_t *)&evt->spinlock, flags);

    evt->bits &= ~mask;

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            reinit_completion(&evt->events[i]);
        }
    }

    spin_unlock_irqrestore((spinlock_t *)&evt->spinlock, flags);

    pr_debug("QPOSMClearEvtgrp: exiting 0\n");
    return QPERR_SUCCESS;
}

int QPOSMWaitEvtgrp(void *evtgrp, u32 mask, u32 *out_events, long timeout_ms)
{
    struct t_event_block *evt;
    unsigned long flags;
    struct completion *wait_array[32];
    int wait_count = 0;
    int i, ret;
    unsigned long timeout_jiffies;

    pr_debug("QPOSMWaitEvtgrp: entered 0x%pK, 0x%X, 0x%pK, %ld\n",
             evtgrp, mask, out_events, timeout_ms);

    if (!evtgrp) {
        pr_debug("QPOSMWaitEvtgrp: NULL event group ID\n");
        return QPERR_PARMS;
    }

    evt = (struct t_event_block *)evtgrp;

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            wait_array[wait_count++] = &evt->events[i];
        }
    }

    if (wait_count == 0)
        return QPERR_PARMS;

    if (timeout_ms < 0) {
        timeout_jiffies = MAX_SCHEDULE_TIMEOUT;
    } else if (timeout_ms == 0) {
        timeout_jiffies = 0;
    } else {
        timeout_jiffies = msecs_to_jiffies(timeout_ms);
    }

    ret = 0;
    while (timeout_jiffies > 0 || timeout_ms < 0) {
        spin_lock_irqsave((spinlock_t *)&evt->spinlock, flags);
        if (evt->bits & mask) {
            if (out_events)
                *out_events = evt->bits;

            evt->bits &= ~mask;

            spin_unlock_irqrestore((spinlock_t *)&evt->spinlock, flags);
            pr_debug("QPOSMWaitEvtgrp: exiting (0x%X) 0\n",
                     out_events ? *out_events : 0);
            return QPERR_SUCCESS;
        }
        spin_unlock_irqrestore((spinlock_t *)&evt->spinlock, flags);

        ret = wait_for_completion_interruptible_timeout(
            wait_array[0],
            timeout_ms < 0 ? HZ / 10 : min(timeout_jiffies, (unsigned long)(HZ / 10))
        );

        if (ret < 0) {
            pr_debug("QPOSMWaitEvtgrp: exiting (interrupted) %d\n", ret);
            return ret;
        }

        if (ret == 0 && timeout_ms >= 0) {
            if (timeout_jiffies <= HZ / 10) {
                pr_debug("QPOSMWaitEvtgrp: exiting (timeout) -110\n");
                return -ETIMEDOUT;
            }
            timeout_jiffies -= HZ / 10;
        }

        reinit_completion(wait_array[0]);
    }

    pr_debug("QPOSMWaitEvtgrp: exiting (timeout) -110\n");
    return -ETIMEDOUT;
}

int QPOSMLockMutex(void *mutex)
{
    return 0;
}

int QPOSMDeleteEvtgrp(void *evtgrp)
{
    struct t_event_block *evt_block;
    int lock_result;
    int ret = -ENOSYS;

    pr_debug("QPOSMDeleteEvtgrp: entered %pK\n", evtgrp);

    if (!evtgrp) {
        pr_debug("QPOSMDeleteEvtgrp: NULL event group ID\n");
        return -EINVAL;
    }

    evt_block = (struct t_event_block *)evtgrp;

    lock_result = QPOSMLockMutex(evt_block->mutexID);
    if (lock_result == 0) {
        pr_debug("QPOSMDeleteEvtgrp: could not lock mutex\n");
        return -EIO;
    }

    if (evt_block->check == 0x69677665) {
        ret = 0;
        evt_block->check = 0x64677665;
        QPOSMDeleteMutex(evt_block->mutexID);
        kfree(evt_block);

        pr_debug("QPOSMDeleteEvtgrp: exiting %d\n", ret);
        return ret;
    } else {
        pr_debug("QPOSMDeleteEvtgrp: invalid event group ID (0x%X)\n", evt_block->check);
        return -EINVAL;
    }
}

int QPOSMDeleteSem(void *sem)
{
    int ret = -ENOSYS;

    pr_debug("QPOSMDeleteSem: entered %pK\n", sem);

    if (!sem) {
        pr_debug("QPOSMDeleteSem: NULL semaphore ID\n");
        return -EINVAL;
    }

    if (*(u32 *)sem == 0x696d6573) {
        ret = 0;
        *(u32 *)sem = 0x646d6573;
        kfree(sem);

        pr_debug("QPOSMDeleteSem: exiting %d\n", ret);
        return ret;
    } else {
        pr_debug("QPOSMDeleteSem: invalid semaphore ID (0x%X)\n", *(u32 *)sem);
        return -EINVAL;
    }
}

void QPOSMDeleteMutex(void *mutex)
{
    if (mutex) {
        QPOSMDeleteSem(mutex);
    }
}

int QPOSMWaitSem(void *sem, int timeout_ms)
{
    if (!sem)
        return -EINVAL;
    return 0;
}

int QPOSMSignalSem(void *sem)
{
    if (!sem)
        return -EINVAL;
    return 0;
}