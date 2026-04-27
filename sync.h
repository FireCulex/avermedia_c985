/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SYNC_H
#define _SYNC_H

#include <linux/types.h>
#include "include/abi/_kspin.h"


/* Synchronization */
unsigned long KeAcquireSpinLockRaiseToDpc(void *lock);
void KeReleaseSpinLock(void *lock, unsigned long flags);
void KeWaitForSingleObject(void *mutex, u8 p2, u8 p3, u8 p4, u32 p5);
void KeReleaseMutex(void *mutex, u8 p2);

/* KS Stream */
struct _KSSTREAM_POINTER;

#define KSSTREAM_POINTER_STATE_UNLOCKED  0
#define KSSTREAM_POINTER_STATE_LOCKED    1

struct _KSSTREAM_POINTER *KsPinGetLeadingEdgeStreamPointer(void *pin, u32 state);
int KsStreamPointerClone(struct _KSSTREAM_POINTER *original,
                         void (*cancel_callback)(struct _KSSTREAM_POINTER *),
                         u32 context_size,
                         struct _KSSTREAM_POINTER **clone_out);
void KsStreamPointerDelete(struct _KSSTREAM_POINTER *sp);
void KsStreamPointerUnlock(struct _KSSTREAM_POINTER *sp, u32 eject);
void KsStreamPointerAdvanceOffsetsAndUnlock(struct _KSSTREAM_POINTER *sp,
                                            u32 in_bytes, u32 out_bytes, u32 eject);

void KsStreamPointerAdvance(struct _KSSTREAM_POINTER *sp);
#endif /* _SYNC_H */
void *KsPinGetParentFilter(struct _KSPIN *ks_pin);
void KsPinAttemptProcessing(struct _KSPIN *ks_pin, u32 flags);
struct _KSSTREAM_POINTER *KsPinGetFirstCloneStreamPointer(struct _KSPIN *ks_pin);
