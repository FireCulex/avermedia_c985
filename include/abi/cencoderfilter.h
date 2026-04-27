/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_CENCODERFILTER_H
#define C985_CENCODERFILTER_H

#include <linux/types.h>
#include "cobject.h"
#include "../../cdevice.h"

struct CEncoderFilter {
    s64 _padding_[14];                     /* 0x00 - 0x70 */
    struct _KSFILTER *m_p_ks_filt;         /* 0x70 */
    struct c_device *m_pDevice;            /* 0x78 */
    enum qp_process_type m_process_name;   /* 0x80 */
};

#endif /* C985_CENCODERFILTER_H */
