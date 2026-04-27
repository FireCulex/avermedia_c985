/* include/ctask_raw_audio.h */
#ifndef _CTASK_RAW_AUDIO_H
#define _CTASK_RAW_AUDIO_H

#include <linux/types.h>
#include "cdevice.h"

struct _HDMI_INFO {
    u16 HActive;        /* 0x00 */
    u16 VActive;        /* 0x02 */
    u16 HTotal;         /* 0x04 */
    u16 VTotal;         /* 0x06 */
    s32 PCLK;           /* 0x08 */
    u8  xCnt;           /* 0x0C */
    u8  ScanMode;       /* 0x0D */
    u8  VPolarity;      /* 0x0E */
    u8  HPolarity;      /* 0x0F */
    u16 Rate;           /* 0x10 */
    u16 __pad;          /* 0x12 - alignment to offset 0x14 */
    u32 QP_InCtrl;      /* 0x14 */
    u32 QP_InRes;       /* 0x18 */
    u32 QP_InSync;      /* 0x1C */
};                      /* size: 0x20 = 32 bytes */

struct CTaskRawAudio {
    int m_dwRawAudioTaskIsRunning;          /* 0x00 */
    u32 __pad04;                            /* 0x04 - alignment padding */
    struct CDevice    *m_pDevice;          /* 0x08 */
    struct ICodecLib   *m_pCodec;           /* 0x10 */
    struct IMpegCodec  *m_pMpegCodec;       /* 0x18 */
    ulong m_hRawAudioTask;                  /* 0x20 */
    struct _HDMI_INFO m_hdmi_video_info;    /* 0x24 */
};                                          /* total: 0x44 */

#endif /* _CTASK_RAW_AUDIO_H */
