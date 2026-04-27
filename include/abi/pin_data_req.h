/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/abi/pin_data_req.h — PIN_DATA_REQ ABI layout
 *
 * From Ghidra PIN_DATA_REQ struct:
 *   0x00  _KSSTREAM_POINTER *  pSrb
 *   0x08  _QP_BUFFER_DESCRIPTOR *  pBufDesc
 *   0x10  ulong  dwId
 *   Total: 0x18
 */
#ifndef _PIN_DATA_REQ_H
#define _PIN_DATA_REQ_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>

struct _KSSTREAM_POINTER;
struct _QP_BUFFER_DESCRIPTOR;

struct PIN_DATA_REQ {
    struct _KSSTREAM_POINTER *pSrb;         /* 0x00 */
    struct _QP_BUFFER_DESCRIPTOR *pBufDesc; /* 0x08 */
    u32 dwId;                               /* 0x10 */
    u32 _pad14;                             /* 0x14 */
};                                          /* total: 0x18 */

static_assert(offsetof(struct PIN_DATA_REQ, pSrb)     == 0x00);
static_assert(offsetof(struct PIN_DATA_REQ, pBufDesc) == 0x08);
static_assert(offsetof(struct PIN_DATA_REQ, dwId)     == 0x10);
static_assert(sizeof(struct PIN_DATA_REQ)             == 0x18);

#endif /* _PIN_DATA_REQ_H */
