/* SPDX-License-Identifier: GPL-2.0 */
/*
 * QP Buffer Descriptor ABI
 *
 * Size: 0x64 = 100 bytes
 */

#ifndef QP_BUFFER_DESCRIPTOR_H
#define QP_BUFFER_DESCRIPTOR_H

#include <linux/types.h>
#include "_qp_scatter_gather.h"


struct _QP_BUFFER_DESCRIPTOR {
    struct _QP_KSSTREAM_HEADER *DataBufferArray; /* 0x00 */
    u32 NumberOfBuffers;                    /* 0x08 */
    u32 ulBufferIndex;                      /* 0x0C */
    u32 ulBufferOffset;                     /* 0x10 */
    u32 ulBufferSize;                       /* 0x14 */
    struct _QP_SCATTER_GATHER *ScatterGatherBuffer;              /* 0x18 */
    u32 NumberOfScatterGatherElements;      /* 0x20 */
    u32 ulDMABufferIndex;                   /* 0x24 */
    u32 ulDMABufferOffset;                  /* 0x28 */
    u32 ulTotalUsed;                        /* 0x2C */
    u32 ulFlags;                            /* 0x30 */
    u8 Status;                              /* 0x34 */
    u8 _pad1[3];                            /* 0x35 */
    void *pBuffer;                          /* 0x38 */
    s64 ulPTS;                              /* 0x40 */
    u32 dwFrameFlags;                       /* 0x48 */
    s64 PictureNumber;                      /* 0x4C */
    s64 DropCount;                          /* 0x54 */
    u32 unAlignedData;                      /* 0x5C */
    u32 unAlignedNumberBytes;               /* 0x60 */
};

typedef struct _QP_BUFFER_DESCRIPTOR _QP_BUFFER_DESCRIPTOR;

#endif /* QP_BUFFER_DESCRIPTOR_H */
