/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_KEVENT_H
#define C985_KEVENT_H

#include <linux/completion.h>

/*
 * Windows _KEVENT: 0x18 bytes (_DISPATCHER_HEADER)
 *
 * Linux struct completion is 0x20 bytes — larger than Windows.
 * This does NOT affect correctness because t_event_block.events[]
 * is only accessed by index, never by absolute ABI offset from
 * another struct. The fields before and after the array (check,
 * bits, mutexID, spinlock) use explicit offsets that are correct
 * regardless of per-element size.
 *
 * We use struct completion directly as the Linux _KEVENT equivalent.
 */
struct _KEVENT {
    struct completion completion;
};

#endif /* C985_KEVENT_H */
