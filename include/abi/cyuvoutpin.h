/* include/abi/cyuvoutpin.h */
#ifndef _CYUVOUTPIN_H
#define _CYUVOUTPIN_H

#include <linux/types.h>
#include "cdatapin.h"

struct tagRECT {
    s32 left;                   /* 0x00 */
    s32 top;                    /* 0x04 */
    s32 right;                  /* 0x08 */
    s32 bottom;                 /* 0x0C */
};                              /* total: 0x10 */

struct tagKS_BITMAPINFOHEADER {
    u32 biSize;                 /* 0x00 */
    s32 biWidth;                /* 0x04 */
    s32 biHeight;               /* 0x08 */
    u16 biPlanes;               /* 0x0C */
    u16 biBitCount;             /* 0x0E */
    u32 biCompression;          /* 0x10 */
    u32 biSizeImage;            /* 0x14 */
    s32 biXPelsPerMeter;        /* 0x18 */
    s32 biYPelsPerMeter;        /* 0x1C */
    u32 biClrUsed;              /* 0x20 */
    u32 biClrImportant;         /* 0x24 */
};                              /* total: 0x28 */

struct tagKS_VIDEOINFOHEADER {
    struct tagRECT rcSource;                    /* 0x00 */
    struct tagRECT rcTarget;                    /* 0x10 */
    u32 dwBitRate;                              /* 0x20 */
    u32 dwBitErrorRate;                         /* 0x24 */
    s64 AvgTimePerFrame;                        /* 0x28 */
    struct tagKS_BITMAPINFOHEADER bmiHeader;    /* 0x30 */
};                                              /* total: 0x58 */

struct FormatData {
    s32 m_image_height;         /* 0x00 */
    s32 m_image_width;          /* 0x04 */
    s32 m_stride_in_bytes;      /* 0x08 */
    s32 m_bit_count;            /* 0x0C */
    u32 m_compression;          /* 0x10 */
};                              /* total: 0x14 */

struct cyuv_out_pin {
    struct c_data_pin base;                      /* 0x0000 - 0xC10F */
    struct tagKS_VIDEOINFOHEADER m_info_hdr;     /* 0xC110 - 0xC167 */
    struct FormatData m_format;                  /* 0xC168 - 0xC17B */
    u32 _pad_c17c;                               /* 0xC17C */
    s64 m_prev_time;                             /* 0xC180 */
};                                               /* total: 0xC188 */

#endif /* _CYUVOUTPIN_H */
