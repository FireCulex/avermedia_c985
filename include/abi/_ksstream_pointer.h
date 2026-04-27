/* include/abi/_ksstream_pointer.h */
#ifndef _KSSTREAM_POINTER_H
#define _KSSTREAM_POINTER_H

#include <linux/types.h>
#include "_ksstream_pointer_offset.h"
#include "_ksstream_header.h"

struct _KSPIN;

struct _KSSTREAM_POINTER {
    void *Context;                              /* 0x00 */
    struct _KSPIN *Pin;                         /* 0x08 */
    struct _KSSTREAM_HEADER *StreamHeader;      /* 0x10 */
    struct _KSSTREAM_POINTER_OFFSET *Offset;    /* 0x18 */
    struct _KSSTREAM_POINTER_OFFSET OffsetIn;   /* 0x20 */
    struct _KSSTREAM_POINTER_OFFSET OffsetOut;  /* 0x30 */
};                                              /* total: 0x40 */

#endif /* _KSSTREAM_POINTER_H */
