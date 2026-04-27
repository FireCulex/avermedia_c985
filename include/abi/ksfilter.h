/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_KSFILTER_H
#define C985_KSFILTER_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>

struct _KSFILTER_DESCRIPTOR;

struct _KSFILTER {
    struct _KSFILTER_DESCRIPTOR *Descriptor; /* 0x00 */
    void                       *Bag;         /* 0x08 */
    void                       *Context;     /* 0x10 */
};

static_assert(offsetof(struct _KSFILTER, Descriptor) == 0x00);
static_assert(offsetof(struct _KSFILTER, Bag)        == 0x08);
static_assert(offsetof(struct _KSFILTER, Context)    == 0x10);

#endif /* C985_KSFILTER_H */
