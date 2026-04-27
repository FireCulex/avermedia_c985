/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_TEVENTBLOCK_H
#define C985_TEVENTBLOCK_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include "_kevent.h"

/*
 * t_event_block - from Ghidra AVerPL33_x64.pdb
 *
 * Windows layout:
 *   0x00   int          check
 *   0x04   int          bits
 *   0x08   void*        mutexID
 *   0x10   _KEVENT[32]  events    (32 * 0x18 = 0x300)
 *   0x310  ulong64      spinlock
 *   Total: 0x318
 *
 * Linux layout differs because struct completion (0x20) is larger
 * than Windows _KEVENT (0x18). This makes the Linux struct larger
 * (0x418 vs 0x318) but does NOT affect correctness:
 *   - check, bits, mutexID are at fixed offsets (0x00, 0x04, 0x08)
 *   - events[] is accessed by index, not absolute offset
 *   - spinlock follows events[] and is accessed as a named field
 *
 * We intentionally do NOT force ABI size parity here.
 */
struct t_event_block {
    int     check;              /* 0x00 */
    int     bits;               /* 0x04 */
    void   *mutexID;            /* 0x08 */
    struct _KEVENT events[32];  /* 0x10 */
    u64     spinlock;           /* Linux: 0x410, Windows: 0x310 */
};

static_assert(offsetof(struct t_event_block, check)   == 0x00);
static_assert(offsetof(struct t_event_block, bits)    == 0x04);
static_assert(offsetof(struct t_event_block, mutexID) == 0x08);
static_assert(offsetof(struct t_event_block, events)  == 0x10);

/* NOTE: spinlock offset and total size intentionally differ from Windows */

#endif /* C985_TEVENTBLOCK_H */
