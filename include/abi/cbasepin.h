#ifndef _CBASEPIN_H
#define _CBASEPIN_H

#include <linux/types.h>
#include "_kspin.h"
#include "cdevice.h"

struct IKsReferenceClock;

struct c_base_pin {
    s64 _padding_[14];                    /* 0x00 - 0x6F */
    u32 m_hStreamLib;                     /* 0x70 */
    u32 m_hTask;                          /* 0x74 */
    u32 m_dwOpenType;                     /* 0x78 */
    u32 _pad7c;                           /* 0x7C */
    void *m_pOpenFormat;                  /* 0x80 */
    KSSTATE m_State;                      /* 0x88 */
    u32 _pad8c;                           /* 0x8C */
    s64 m_picture_num;                    /* 0x90 */
    s64 m_dropped_cnt;                    /* 0x98 */
    struct _KSPIN *m_p_ks_pin;            /* 0xA0 */
    s64 m_duration;                       /* 0xA8 */
    s64 m_start_time;                     /* 0xB0 */
    u8 m_discontinuity;                   /* 0xB8 */
    u8 _padb9[7];                         /* 0xB9 */
    struct IKsReferenceClock *m_p_clock;  /* 0xC0 */
    u32 m_frame_size;                     /* 0xC8 */
    int m_EOS;                            /* 0xCC */
    int m_bBufferPartialFill;             /* 0xD0 */
    int m_bBufferFrameAligned;            /* 0xD4 */
    int m_bDisabled;                      /* 0xD8 */
    u32 _paddc;                           /* 0xDC */
    struct CDevice *m_pDevice;           /* 0xE0 */
    u32 m_dwFrameWidth;                   /* 0xE8 */
    u32 m_dwFrameHeight;                  /* 0xEC */
};                                        /* total: 0xF0 */

#endif
