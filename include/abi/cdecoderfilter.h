/* include/cdecoder_filter.h */
#ifndef _CDECODER_FILTER_H
#define _CDECODER_FILTER_H

#include <linux/types.h>

struct CDecoderFilter {
    s64 _padding_00;            /* 0x00 */
    s64 _padding_08;            /* 0x08 */
    s64 _padding_10;            /* 0x10 */
    s64 _padding_18;            /* 0x18 */
    s64 _padding_20;            /* 0x20 */
    s64 _padding_28;            /* 0x28 */
    s64 _padding_30;            /* 0x30 */
    s64 _padding_38;            /* 0x38 */
    s64 _padding_40;            /* 0x40 */
    s64 _padding_48;            /* 0x48 */
    s64 _padding_50;            /* 0x50 */
    s64 _padding_58;            /* 0x58 */
    s64 _padding_60;            /* 0x60 */
    s64 _padding_68;            /* 0x68 */
    void *m_p_ks_filt;          /* 0x70 */
    ulong m_hTask;              /* 0x78 */
};                              /* total: 0x80 */

#endif /* _CDECODER_FILTER_H */
