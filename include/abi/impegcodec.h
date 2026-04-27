/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IMpegCodec - MPEG codec interface ABI
 *
 * Size: 0xC8 = 200 bytes
 */

#ifndef IMPEGCODEC_H
#define IMPEGCODEC_H

#include <linux/types.h>

struct IMpegCodec {
    void *InitDevice;                       /* 0x00 */
    void *Release;                          /* 0x08 */
    void *Reset;                            /* 0x10 */
    void *Set;                              /* 0x18 */
    void *Get;                              /* 0x20 */
    void *AllocEncodeTask;                  /* 0x28 */
    void *AllocDecodeTask;                  /* 0x30 */
    void *ReleaseTask;                      /* 0x38 */
    void *Open;                             /* 0x40 */
    void *Close;                            /* 0x48 */
    void *Start;                            /* 0x50 */
    void *Stop;                             /* 0x58 */
    void *Acquire;                          /* 0x60 */
    void *Pause;                            /* 0x68 */
    void *Step;                             /* 0x70 */
    void *SetRate;                          /* 0x78 */
    void *GetRate;                          /* 0x80 */
    void *BeginFlush;                       /* 0x88 */
    void *Flush;                            /* 0x90 */
    void *EndFlush;                         /* 0x98 */
    void *AddBuffer;                        /* 0xA0 */
    void *CancelBuffer;                     /* 0xA8 */
    void *TimeoutBuffer;                    /* 0xB0 */
    void *GetTime;                          /* 0xB8 */
    void *XferData;                         /* 0xC0 */
};

#endif /* IMPEGCODEC_H */