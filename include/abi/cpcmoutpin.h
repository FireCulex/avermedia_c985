#ifndef _CPCMOUTPIN_H
#define _CPCMOUTPIN_H

#include <linux/types.h>
#include "cdatapin.h"

struct IMpegCodec;

struct cpcm_out_pin {
    struct c_data_pin base;          /* 0x0000 */
    struct IMpegCodec *m_pMpegCodec; /* 0xC110 */
    int m_bWaveInFilter;             /* 0xC118 */
    u32 _pad_c11c;                   /* 0xC11C */
};                                   /* total: 0xC120 */

#endif
