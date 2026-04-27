/* SPDX-License-Identifier: GPL-2.0 */
/*
 * QP KSStream Header ABI
 *
 * Size: 0x2C = 44 bytes
 */

#ifndef _QP_KSSTREAM_HEADER_H
#define _QP_KSSTREAM_HEADER_H

#include <linux/types.h>

struct _QP_KSTIME {
    s64 Time;           /* 0x00 */
    u32 Numerator;      /* 0x08 */
    u32 Denominator;    /* 0x0C */
};                      /* total: 0x10 */

typedef struct _QP_KSTIME _QP_KSTIME;

struct _QP_KSSTREAM_HEADER {
    struct _QP_KSTIME PresentationTime;  /* 0x00 */
    s64 Duration;                        /* 0x10 */
    u32 FrameExtent;                     /* 0x18 */
    u32 DataUsed;                        /* 0x1C */
    void *Data;                          /* 0x20 */
    u32 OptionsFlags;                    /* 0x28 */
};                                       /* total: 0x2C */

typedef struct _QP_KSSTREAM_HEADER _QP_KSSTREAM_HEADER;

#endif /* _QP_KSSTREAM_HEADER_H */
