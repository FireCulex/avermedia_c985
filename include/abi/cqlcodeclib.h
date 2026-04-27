/* include/abi/cqlcodeclib.h */
#ifndef _CQLCODECLIB_H
#define _CQLCODECLIB_H

#include <linux/types.h>
#include "cobject.h"
#include "cqlcodec.h"
#include "../../qperrors.h"

struct CUsbInterface;
struct CUsbCntl;
struct CPCIeCntl;
struct CVideoDecoder;
struct CVideoEncoder;
struct CAudioCodec;
struct CTuner;
struct CTVAudio;
struct CI2C;

struct _QPMPGCODEC_INITDATA {
    u8 _data[224];
};

struct _QPVIDDECODER_INITDATA {
    u8 _data[12];
};

struct _QPVIDENCODER_INITDATA {
    u8 _data[8];
};

struct _QPTUNER_INITDATA {
    u8 _data[4];
};

struct _QPTVAUDIO_INITDATA {
    u8 _data[4];
};

struct _QPAUDCODEC_INITDATA {
    u8 _data[4];
};

struct _QPI2C_INITDATA {
    u8 _data[12];
};

struct _QPCODECLIB_INITDATA {
    int bDownloadFW;                                    /* 0x00 */
    u32 _pad04;                                         /* 0x04 */
    struct _QPMPGCODEC_INITDATA mpgCodecInitData;       /* 0x08 */
    struct _QPVIDDECODER_INITDATA vidDecoderInitData;    /* 0xE8 */
    struct _QPVIDENCODER_INITDATA vidEncoderInitData;    /* 0xF4 */
    struct _QPTUNER_INITDATA tunerInitData;              /* 0xFC */
    struct _QPTVAUDIO_INITDATA tvAudioInitData;          /* 0x100 */
    struct _QPAUDCODEC_INITDATA audCodecInitData;        /* 0x104 */
    struct _QPI2C_INITDATA i2cInitData;                  /* 0x108 */
    struct _QPI2C_INITDATA i2cInitDataEx;                /* 0x114 */
};                                                       /* total: 0x120 */

/* ICodecLib interface vtable */
struct ICodecLib {
    _EQPErrors (*InitDevice)(struct ICodecLib *, _EQPErrors (*)(void *, u32, void *), void *);  /* 0x00 */
    _EQPErrors (*Release)(struct ICodecLib *);                      /* 0x08 */
    _EQPErrors (*Reset)(struct ICodecLib *);                        /* 0x10 */
    _EQPErrors (*Set)(struct ICodecLib *, struct _TQP_GUID *, u32, u32, void *, void *, u32);   /* 0x18 */
    _EQPErrors (*Get)(struct ICodecLib *, struct _TQP_GUID *, u32, u32, void *, void *, u32 *); /* 0x20 */
    _EQPErrors (*GetMpegCodec)(struct ICodecLib *, struct IMpegCodec **);   /* 0x28 */
    _EQPErrors (*GetVideoDecoder)(struct ICodecLib *, void **);     /* 0x30 */
    _EQPErrors (*GetVideoEncoder)(struct ICodecLib *, void **);     /* 0x38 */
    _EQPErrors (*GetAudioCodec)(struct ICodecLib *, void **);       /* 0x40 */
    _EQPErrors (*GetTuner)(struct ICodecLib *, void **);            /* 0x48 */
    _EQPErrors (*GetTVAudio)(struct ICodecLib *, void **);          /* 0x50 */
    _EQPErrors (*Disable)(struct ICodecLib *);                      /* 0x58 */
    _EQPErrors (*Enable)(struct ICodecLib *);                       /* 0x60 */
};                                                                  /* total: 0x68 */

struct CQLCodecLib {
    struct CObject m_Object;                                /* 0x00 */
    struct ICodecLib m_iCodecLib;                           /* 0x38 */
    void *m_pDO;                                            /* 0xA0 */
    void *m_PDOLayered;                                     /* 0xA8 */
    struct _QPCODECLIB_INITDATA m_InitData;                 /* 0xB0 */
    _EQPErrors (*m_pDeviceCallback)(void *, u32, void *);   /* 0x1D0 */
    void *m_callbackContext;                                /* 0x1D8 */
    struct CUsbInterface *m_pUsbInterface;                  /* 0x1E0 */
    struct CPCIeCntl *m_pPCIeCntl;                          /* 0x1E8 */
    struct CUsbCntl *m_pUsbCntl;                            /* 0x1F0 */
    struct CQLCodec *m_pMpgCodec;                           /* 0x1F8 */
    struct CI2C *m_pI2CProvider;                            /* 0x200 */
    struct CI2C *m_pI2CProviderEx;                          /* 0x208 */
    struct CVideoDecoder *m_pVidDecoder;                    /* 0x210 */
    struct CVideoEncoder *m_pVidEncoder;                    /* 0x218 */
    struct CAudioCodec *m_pAudCodec;                        /* 0x220 */
    struct CTuner *m_pTuner;                                /* 0x228 */
    struct CTVAudio *m_pTVAudio;                            /* 0x230 */
    u32 m_dwDeviceError;                                    /* 0x238 */
    u32 _pad23C;                                            /* 0x23C */
};                                                          /* total: 0x240 */

#endif /* _CQLCODECLIB_H */
