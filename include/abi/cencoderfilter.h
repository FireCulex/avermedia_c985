/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_CENCODERFILTER_H
#define C985_CENCODERFILTER_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include "cppobject.h"
#include "ksfilter.h"
#include "qp_process_type.h"
#include "cdevice.h"

struct CEncoderFilter {
    struct CppObject     base;           /* 0x00 - CppObject base class */
    struct _KSFILTER    *m_p_ks_filt;   /* 0x70 */
    struct CDevice     *m_pDevice;      /* 0x78 */
    enum QP_PROCESS_TYPE m_process_name; /* 0x80 */
};

static_assert(offsetof(struct CEncoderFilter, base)           == 0x00);
static_assert(offsetof(struct CEncoderFilter, m_p_ks_filt)    == 0x70);
static_assert(offsetof(struct CEncoderFilter, m_pDevice)      == 0x78);
static_assert(offsetof(struct CEncoderFilter, m_process_name) == 0x80);

#define CENCODERFILTER_WHOAMI  0x103fc

#endif /* C985_CENCODERFILTER_H */
