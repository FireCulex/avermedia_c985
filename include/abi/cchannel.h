/* include/abi/cchannel.h */
#ifndef _CCHANNEL_H
#define _CCHANNEL_H

#include <linux/types.h>
#include "cobject.h"
#include "cqueue.h"
#include "qp_buffer_descriptor.h"
#include "../../qperrors.h"

struct CChannel;
struct CTask;

/* Channel direction */
typedef enum _CHANNEL_DIRECTION {
    CHANNEL_DIRECTION_IN = 0,
    CHANNEL_DIRECTION_OUT = 1,
} _CHANNEL_DIRECTION;

/* Channel open type */
typedef enum _QPMPGCODEC_OPEN_TYPE {
    QPMPGCODEC_ENC_MPEG_OUT     = 0,
    QPMPGCODEC_ENC_AES_OUT      = 1,
    QPMPGCODEC_ENC_VBI_OUT      = 2,
    QPMPGCODEC_ENC_INDEX_OUT    = 3,
    QPMPGCODEC_ENC_YUV_IN       = 4,
    QPMPGCODEC_ENC_PCM_IN       = 5,
    QPMPGCODEC_ENC_PCM_OUT      = 6,
    QPMPGCODEC_ENC_YUV_OUT      = 7,
    QPMPGCODEC_DEC_MPEG_IN      = 8,
    QPMPGCODEC_DEC_AES_IN       = 9,
    QPMPGCODEC_DEC_INDEX_IN     = 10,
    QPMPGCODEC_DEC_YUV_OUT      = 11,
    QPMPGCODEC_DEC_PCM_OUT      = 12,
    QPMPGCODEC_DEC_VBI_IN       = 13,
    QPMPGCODEC_CODEC_VIRTUAL    = 14,
    QPMPGCODEC_ENC_ACTIVITY_OUT = 15,
} _QPMPGCODEC_OPEN_TYPE;

/* Channel state */
typedef enum _QPSTATE {
    QPSTATE_STOP = 0,
    QPSTATE_ACQUIRE = 1,
    QPSTATE_PAUSE = 2,
    QPSTATE_RUN = 3,
} _QPSTATE;

/* Channel vtable function pointer typedefs */
typedef _EQPErrors (*channel_open_fn)(struct CChannel *, ulong, u32, void *,
                                      _EQPErrors (*)(void *, u32, void *), void *);
typedef _EQPErrors (*channel_simple_fn)(struct CChannel *);
typedef _EQPErrors (*channel_step_fn)(struct CChannel *, u32);
typedef _EQPErrors (*channel_set_rate_fn)(struct CChannel *, int);
typedef _EQPErrors (*channel_get_rate_fn)(struct CChannel *, int *);
typedef _EQPErrors (*channel_buf_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR *);
typedef _EQPErrors (*channel_cancel_buf_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR *, int);
typedef int (*channel_get_buffer_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR **,
                                     u8 **, u32 *);
typedef int (*channel_get_buffer_yuv_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR **,
                                         u8 **, u32 *, u8 **, u32 *);
typedef int (*channel_get_buffer_yuv_ras_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR **,
                                             u8 **, u32 *, u8 **, u32 *, u8 **, u32 *);
typedef void (*channel_complete_buf_fn)(struct CChannel *, struct _QP_BUFFER_DESCRIPTOR *);
typedef _EQPErrors (*channel_get_resolution_fn)(struct CChannel *, u32 *, u32 *);
typedef _EQPErrors (*channel_get_yuv_format_fn)(struct CChannel *, u32 *);
typedef _EQPErrors (*channel_device_callback_fn)(void *, u32, void *);

struct CChannel {
    /* CObject base (0x00 - 0x37) */
    struct CObject m_Object;                            /* 0x00 */

    /* Channel vtable function pointers */
    channel_open_fn Open;                               /* 0x38 */
    channel_simple_fn Close;                            /* 0x40 */
    channel_simple_fn Start;                            /* 0x48 */
    channel_simple_fn Stop;                             /* 0x50 */
    channel_simple_fn Acquire;                          /* 0x58 */
    channel_simple_fn Pause;                            /* 0x60 */
    channel_step_fn Step;                               /* 0x68 */
    channel_set_rate_fn SetRate;                        /* 0x70 */
    channel_get_rate_fn GetRate;                        /* 0x78 */
    channel_simple_fn BeginFlush;                       /* 0x80 */
    channel_simple_fn Flush;                            /* 0x88 */
    channel_simple_fn EndFlush;                         /* 0x90 */
    channel_buf_fn AddBuffer;                           /* 0x98 */
    channel_cancel_buf_fn CancelBuffer;                 /* 0xA0 */
    channel_buf_fn TimeoutBuffer;                       /* 0xA8 */
    channel_buf_fn XferData;                            /* 0xB0 */
    channel_get_buffer_fn GetBuffer;                    /* 0xB8 */
    channel_get_buffer_yuv_fn GetBufferYUV;             /* 0xC0 */
    channel_get_buffer_yuv_ras_fn GetBufferYUVRAS;      /* 0xC8 */
    channel_complete_buf_fn CompleteBuffer;              /* 0xD0 */
    channel_simple_fn BeginChannelChange;               /* 0xD8 */
    channel_simple_fn EndChannelChange;                 /* 0xE0 */
    channel_get_resolution_fn GetResolution;            /* 0xE8 */
    channel_get_yuv_format_fn GetYUVFormat;             /* 0xF0 */

    /* Channel data members */
    u32 m_dwOpenFlags;                                  /* 0xF8 */
    u32 _padFC;                                         /* 0xFC */
    channel_device_callback_fn m_pDeviceCallback;       /* 0x100 */
    void *m_callbackContext;                             /* 0x108 */
    u32 m_hTask;                                        /* 0x110 */
    u32 m_hChannel;                                     /* 0x114 */
    int m_dataType;                                     /* 0x118 */
    int m_FWDataType;                                   /* 0x11C */
    _CHANNEL_DIRECTION m_ChannelDirection;              /* 0x120 */
    _QPMPGCODEC_OPEN_TYPE m_ChannelType;                /* 0x124 */
    struct CTask *m_pTask;                              /* 0x128 */
    int m_bOpened;                                      /* 0x130 */
    _QPSTATE m_State;                                   /* 0x134 */
    int m_bPaused;                                      /* 0x138 */
    u32 _pad13C;                                        /* 0x13C */
    struct QUEUE_ENTRY m_Entries[256];                   /* 0x140 */
    struct c_queue *m_pFreeQueue;                       /* 0x1140 */
    struct c_queue *m_pDataRequestQueue;                /* 0x1148 */
    struct c_queue *m_pDataPendingQueue;                /* 0x1150 */
    u64 m_llStartTime;                                  /* 0x1158 */
    int m_bFlushing;                                    /* 0x1160 */
    int m_bByteSwap;                                    /* 0x1164 */
    u64 m_ullCntBytes;                                  /* 0x1168 */
    u8 m_savedDword[4];                                 /* 0x1170 */
    u32 m_savedNumberBytes;                             /* 0x1174 */
    s64 m_llBasePts;                                    /* 0x1178 */
    s64 m_llPrevPts;                                    /* 0x1180 */
    s64 m_llAddPts;                                     /* 0x1188 */
};                                                      /* total: 0x1190 */

#endif /* _CCHANNEL_H */
