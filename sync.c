/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sync.c - Windows Kernel Synchronization & KS Stream API Stubs for Linux
 */

#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/export.h>
#include "sync.h"
#include "include/abi/_kspin.h"
#include "pins.h"

/* ---- Spinlock / Mutex stubs ---- */

unsigned long KeAcquireSpinLockRaiseToDpc(void *lock)
{
    unsigned long flags;

    if (!lock)
        return 0;

    spin_lock_irqsave((spinlock_t *)lock, flags);
    return flags;
}
EXPORT_SYMBOL_GPL(KeAcquireSpinLockRaiseToDpc);

void KeReleaseSpinLock(void *lock, unsigned long flags)
{
    if (!lock)
        return;

    spin_unlock_irqrestore((spinlock_t *)lock, flags);
}
EXPORT_SYMBOL_GPL(KeReleaseSpinLock);

void KeWaitForSingleObject(void *mutex, u8 param_2, u8 param_3, u8 param_4, u32 param_5)
{
    if (!mutex)
        return;

    mutex_lock((struct mutex *)mutex);
}
EXPORT_SYMBOL_GPL(KeWaitForSingleObject);

void KeReleaseMutex(void *mutex, u8 param_2)
{
    if (!mutex)
        return;

    mutex_unlock((struct mutex *)mutex);
}
EXPORT_SYMBOL_GPL(KeReleaseMutex);

/* ---- KS Stream Pointer functions ---- */

struct _KSSTREAM_POINTER *KsPinGetLeadingEdgeStreamPointer(void *pin, u32 state)
{
    if (!pin)
        return NULL;

    return (struct _KSSTREAM_POINTER *)pin;
}
EXPORT_SYMBOL_GPL(KsPinGetLeadingEdgeStreamPointer);

int KsStreamPointerClone(struct _KSSTREAM_POINTER *original,
                         void (*cancel_callback)(struct _KSSTREAM_POINTER *),
                         u32 context_size,
                         struct _KSSTREAM_POINTER **clone_out)
{
    struct _KSSTREAM_POINTER *clone;

    if (!original || !clone_out)
        return -EINVAL;

    clone = kzalloc(sizeof(*clone) + context_size, GFP_KERNEL);
    if (!clone)
        return -ENOMEM;

    clone->StreamHeader = original->StreamHeader;
    clone->Pin = original->Pin;
    clone->Offset = original->Offset;
    clone->OffsetIn = original->OffsetIn;
    clone->OffsetOut = original->OffsetOut;
    clone->Context = original->Context;

    *clone_out = clone;
    return 0;
}
EXPORT_SYMBOL_GPL(KsStreamPointerClone);


void KsStreamPointerDelete(struct _KSSTREAM_POINTER *sp)
{
    if (!sp)
        return;

    kfree(sp);
}
EXPORT_SYMBOL_GPL(KsStreamPointerDelete);

void KsStreamPointerUnlock(struct _KSSTREAM_POINTER *sp, u32 flags)
{
    if (!sp)
        return;
}
EXPORT_SYMBOL_GPL(KsStreamPointerUnlock);

void KsStreamPointerAdvanceOffsetsAndUnlock(struct _KSSTREAM_POINTER *sp,
                                            u32 in_bytes,
                                            u32 out_bytes,
                                            u32 eject)
{
    if (!sp)
        return;

    if (sp->Offset) {
        if (sp->Offset->Remaining >= out_bytes)
            sp->Offset->Remaining -= out_bytes;
        else
            sp->Offset->Remaining = 0;
    }
}
EXPORT_SYMBOL_GPL(KsStreamPointerAdvanceOffsetsAndUnlock);

/* In sync.c - add implementation */
void KsStreamPointerAdvance(struct _KSSTREAM_POINTER *sp)
{
    if (!sp)
        return;
}
EXPORT_SYMBOL_GPL(KsStreamPointerAdvance);

void *KsPinGetParentFilter(struct _KSPIN *ks_pin)
{
    if (!ks_pin || !ks_pin->Context)
        return NULL;

    /*
     * In our v4l2 model, the Context is the c985_poc device.
     * The "parent filter" is equivalent to the device structure.
     */
    pr_debug("%s: ks_pin=%p, parent=%p\n", __func__, ks_pin, ks_pin->Context);
    return ks_pin->Context;
}

/* ============================================
 * KsPinAttemptProcessing
 * Triggers the next buffer processing cycle
 * ============================================ */
void KsPinAttemptProcessing(struct _KSPIN *ks_pin, u32 flags)
{
    struct c985_poc *d;

    if (!ks_pin || !ks_pin->Context)
        return;

    d = (struct c985_poc *)ks_pin->Context;

    /*
     * In v4l2, this wakes up the streaming thread to process
     * more buffers. We signal that the encoder should continue
     * processing the next buffer in the queue.
     */
    pr_debug("%s: ks_pin=%p flags=%u, encoder_running=%d\n",
             __func__, ks_pin, flags, d->encoder_running);

    /*
     * If encoder is running, it will automatically pick up
     * the next buffer from the queue. No explicit wake needed
     * in this model.
     */
}

/* ============================================
 * KsPinGetFirstCloneStreamPointer
 * Gets the next available buffer from the v4l2 queue
 * ============================================ */
struct _KSSTREAM_POINTER *KsPinGetFirstCloneStreamPointer(struct _KSPIN *ks_pin)
{
    struct c985_poc *d;
    struct c985_buffer *buf;
    unsigned long flags;

    if (!ks_pin || !ks_pin->Context) {
        pr_debug("%s: Invalid ks_pin or Context\n", __func__);
        return NULL;
    }

    /* Context points to c985_poc device structure */
    d = (struct c985_poc *)ks_pin->Context;

    /* Get the next buffer from our tracked list */
    spin_lock_irqsave(&d->buf_lock, flags);

    if (list_empty(&d->buf_list)) {
        spin_unlock_irqrestore(&d->buf_lock, flags);
        pr_debug("%s: No buffers in queue\n", __func__);
        return NULL;
    }

    /* Get first buffer from the list */
    buf = list_first_entry(&d->buf_list, struct c985_buffer, list);

    spin_unlock_irqrestore(&d->buf_lock, flags);

    /* Return the stream pointer from the buffer's SRB */
    if (buf->queue_entry && buf->queue_entry->Data) {
        struct pin_data_req *srb = buf->queue_entry->Data;
        pr_debug("%s: ks_pin=%p, stream_ptr=%p (buf=%p)\n",
                 __func__, ks_pin, srb->pSrb, buf);
        return srb->pSrb;
    }

    pr_debug("%s: No stream pointer in buffer\n", __func__);
    return NULL;
}
