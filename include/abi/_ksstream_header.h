/* include/abi/_ksstream_header.h */
#ifndef _KSSTREAM_HEADER_H
#define _KSSTREAM_HEADER_H

#include <linux/types.h>

struct KSTIME {
    s64 Time;           /* 0x00 */
    u32 Numerator;      /* 0x08 */
    u32 Denominator;    /* 0x0C */
};                      /* total: 0x10 */

struct _KSSTREAM_HEADER {
    u32 Size;                       /* 0x00 */
    u32 TypeSpecificFlags;          /* 0x04 */
    struct KSTIME PresentationTime; /* 0x08 */
    s64 Duration;                   /* 0x18 */
    u32 FrameExtent;                /* 0x20 */
    u32 DataUsed;                   /* 0x24 */
    void *Data;                     /* 0x28 */
    u32 OptionsFlags;               /* 0x30 */
    u32 Reserved;                   /* 0x34 */
};                                  /* total: 0x38 */

#endif /* _KSSTREAM_HEADER_H */
