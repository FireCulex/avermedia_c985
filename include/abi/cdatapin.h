#ifndef _CDATAPIN_H
#define _CDATAPIN_H

#include <linux/types.h>
#include "cbasepin.h"
#include "cppqueue.h"
#include "qp_buffer_descriptor.h"
#include "_qp_ksstream_header.h"
#include "pin_data_req.h"
#include "cdevice.h"

struct c_data_queue;

struct c_data_pin {
    struct c_base_pin base;                      /* 0x0000 */
    struct QUEUE_ENTRY_CPP m_Entries[256];       /* 0x00F0 */
    struct PIN_DATA_REQ m_StreamSRBs[256];       /* 0x18F0 */
    struct _QP_BUFFER_DESCRIPTOR m_BufDesces[256]; /* 0x30F0 */
    struct _QP_KSSTREAM_HEADER m_DataBufferArray[256]; /* 0x94F0 */
    struct c_data_queue *m_pFreeQueue;           /* 0xC0F0 */
    struct c_data_queue *m_pDataRequestQueue;    /* 0xC0F8 */
    struct CDevice *m_pDevice;                  /* 0xC100 */
    u32 m_dwDropCounter;                         /* 0xC108 */
    int m_bIsFirstFrame;                         /* 0xC10C */
};                                               /* total: 0xC110 */

#endif
