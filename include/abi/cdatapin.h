#ifndef _CDATAPIN_H
#define _CDATAPIN_H

#include <linux/types.h>
#include "cbasepin.h"
#include "cqueue.h"
#include "qp_buffer_descriptor.h"
#include "_qp_ksstream_header.h"

struct c_device;
struct c_data_queue;

struct pin_data_req {
    void *pSrb;                              /* 0x00 */
    struct _QP_BUFFER_DESCRIPTOR *pBufDesc;  /* 0x08 */
    u32 dwId;                                /* 0x10 */
    u32 _pad14;                              /* 0x14 */
};                                           /* total: 0x18 */

struct QUEUE_ENTRY_CPP {
    struct pin_data_req *Data;               /* 0x00 */
    struct QUEUE_ENTRY_CPP *pNext;           /* 0x08 */
    u64 _pad10;                              /* 0x10 */
};                                           /* total: 0x18 */

struct c_data_pin {
    struct c_base_pin base;                      /* 0x0000 */
    struct QUEUE_ENTRY_CPP m_Entries[256];       /* 0x00F0 */
    struct pin_data_req m_StreamSRBs[256];       /* 0x18F0 */
    struct _QP_BUFFER_DESCRIPTOR m_BufDesces[256]; /* 0x30F0 */
    struct _QP_KSSTREAM_HEADER m_DataBufferArray[256]; /* 0x94F0 */
    struct c_data_queue *m_pFreeQueue;           /* 0xC0F0 */
    struct c_data_queue *m_pDataRequestQueue;    /* 0xC0F8 */
    struct c_device *m_pDevice;                  /* 0xC100 */
    u32 m_dwDropCounter;                         /* 0xC108 */
    int m_bIsFirstFrame;                         /* 0xC10C */
};                                               /* total: 0xC110 */

#endif
