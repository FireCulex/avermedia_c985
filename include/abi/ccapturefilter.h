/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_CCAPTUREFILTER_H
#define C985_CCAPTUREFILTER_H

#include <linux/types.h>
#include <linux/build_bug.h>
#include <linux/stddef.h>
#include "cppobject.h"
#include "ksfilter.h"
#include "qp_process_type.h"
#include "cdevice.h"

struct CCaptureFilter {
    struct CppObject     base;           /* 0x00 - CppObject base class */
    struct _KSFILTER    *m_p_ks_filt;   /* 0x70 */
    struct CDevice     *m_pDevice;      /* 0x78 */
    enum QP_PROCESS_TYPE m_process_name; /* 0x80 */
};

static_assert(offsetof(struct CCaptureFilter, base)           == 0x00);
static_assert(offsetof(struct CCaptureFilter, m_p_ks_filt)    == 0x70);
static_assert(offsetof(struct CCaptureFilter, m_pDevice)      == 0x78);
static_assert(offsetof(struct CCaptureFilter, m_process_name) == 0x80);

#define CCAPTUREFILTER_WHOAMI  0x103ea

#endif /* C985_CCAPTUREFILTER_H */
