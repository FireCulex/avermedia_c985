/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CQLCODEC_H
#define CQLCODEC_H

#include "avermedia_c985.h"
#include "structs.h"
#include "qperrors.h"
#include "include/abi/qp_buffer_descriptor.h"


/* ============================================
 * Encoder register offsets (chip_type & 0xe != 0)
 * ============================================ */
#define ENC_REG_MESSAGE             0x6fc
#define ENC_REG_SYSTEM_CONTROL      0x6f8
#define ENC_REG_PICTURE_RESOLUTION  0x6f4
#define ENC_REG_INPUT_CONTROL       0x6f0
#define ENC_REG_RATE_CONTROL        0x6ec
#define ENC_REG_BIT_RATE            0x6e8
#define ENC_REG_FILTER_CONTROL      0x6e4
#define ENC_REG_GOP_LOOP_FILTER     0x6e0
#define ENC_REG_OUT_PIC_RESOLUTION  0x6dc
#define ENC_REG_ET_CONTROL          0x6d8
#define ENC_REG_BLOCK_SIZE          0x6d8
#define ENC_REG_AUDIO_CONTROL_PARAM 0x6d4
#define ENC_REG_AUDIO_CONTROL_EX    0x6d0

/* ============================================
 * Default control values
 * ============================================ */
#define DEFAULT_AO_CONTROLS     0x17  /* bits 0,1,2,4 set; bits 3,5,6 clear */
#define DEFAULT_AUD_CONTROLS    0x1202

/* ============================================
 * VIU - Video Input Unit defaults
 * ============================================ */
#define VIU_MODE              0x02
#define VIU_FORMAT            0x00
#define VIU_START_PIXEL       0x00
#define VIU_START_LINE        0x00
#define VIU_CLK_EDGE          0x00
#define VIU_SYNC_CODE1        0xf1f1f1da
#define VIU_SYNC_CODE2        0xb6f1f1b6

/* ============================================
 * AI - Audio Input defaults
 * ============================================ */
#define AI_MSB                0x01
#define AI_LRCLK              0x01
#define AI_BCLK               0x00
#define AI_I2S                0x01
#define AI_RJ                 0x00

/* ============================================
 * AO - Audio Output defaults
 * ============================================ */
#define AO_ENABLE             0x01
#define AO_MSB                0x01
#define AO_LRCLK              0x01
#define AO_BCLK               0x00
#define AO_I2S                0x01
#define AO_RJ                 0x01
#define AO_S                  0x00

/* ============================================
 * VO - Video Output defaults
 * ============================================ */
#define VO_ENABLE             0x00   /* disabled */

/* ============================================
 * PCI / Interrupt registers
 * ============================================ */
#define REG_PCI_INT_STATUS  0x4030

/* DM interrupt types for DM_ClearInterrupt() */
#define DM_INT_ARM_MSG      0x01    /* ARM message interrupt */
#define DM_INT_DMA_WRITE    0x02    /* DMA write complete */
#define DM_INT_DMA_READ     0x04    /* DMA read complete */

/* ============================================
 * ARM communication registers
 * ============================================ */
#define REG_ARM_RESP_DATA       0x6CC
#define HOST_TO_ARM_TRIGGER     0x2000000
/* Host to ARM trigger (write 0x2000000 to signal ARM) */
#define REG_HOST_TO_ARM_TRIG    0x24
/* PCI interrupt status - read to check pending interrupts */
#define REG_INT_STATUS          0x30

/* ============================================
 * Public API - Device init/remove
 * ============================================ */

int  cqlcodec_init_device(struct pci_dev *pdev, const struct pci_device_id *id);


/* ============================================
 * Public API - Audio/Video output control
 * ============================================ */
void cqlcodec_ao_switch(struct c985_poc *d, int disable);
void cqlcodec_vo_switch(struct c985_poc *d, int disable);

/* ============================================
 * Public API - Settings and firmware
 * ============================================ */
void cqlcodec_load_default_settings(struct c985_poc *d);
int cqlcodec_fw_download(struct c985_poc *d, int do_reset);

int CQLCodec_InitializeMemory(struct c985_poc *d);

/* ============================================
 * Public API - GPIO
 * ============================================ */
void gpio_set_defaults(struct c985_poc *d);
int CQLCodec_SetGPIOBitValue(struct c985_poc *param_1, struct gpio_bit_value *param_2);
int CQLCodec_SetGPIOBitDirection(struct c985_poc *d, struct gpio_bit_direction *param_2);

/* ============================================
 * Public API - Configuration
 * ============================================ */
_EQPErrors CQLCodec_UpdateMiscConfig(struct cql_codec *codec, u32 task_id);
int CQLCodec_UpdateEncoderConfig(struct c985_poc *param_1, u32 param_2, int param_3);

/* ============================================
 * Public API - Library init
 * ============================================ */
int CQLCodecLib_Init(struct cql_codec_lib *param_1);
int CQLCodecLib_CreatePCIe(struct cql_codec_lib *param_1);
int CQLCodecLib_InitPCIe(struct cql_codec_lib *param_1);
int CQLCodecLib_CreatePeripherals(struct cql_codec_lib *param_1);
int CQLCodecLib_CreatePeripheralsVidDecoder(struct cql_codec_lib *param_1);
int CQLCodecLib_CreateInitUSB(struct cql_codec_lib *param_1, u32 p0, u32 p1, u32 p2, u32 p3, u32 mode);

/* ============================================
 * Public API - Peripheral constructors
 * ============================================ */

struct c_tuner *CTuner_Constructor(struct c_tuner *param_1, struct CObject *param_2, int param_3);
struct c_tvaudio *CTVAudio_Constructor(struct c_tvaudio *param_1, struct CObject *param_2, int param_3);
struct c_video_encoder *CVideoEncoder_Constructor(struct c_video_encoder *param_1,
                                                  struct CObject *param_2, int param_3);
struct c_audio_codec *CAudioCodec_Constructor(struct c_audio_codec *param_1,
                                              struct CObject *param_2, int param_3,
                                              void *param_4, void *param_5);

/* Video encoder constructors */
struct c_saa7128 *CSaa7128_Constructor(struct c_saa7128 *param_1, struct CObject *param_2,
                                       int param_3, void *param_4, char param_5);
struct c_adi7393 *CADI7393_Constructor(struct c_adi7393 *param_1, struct CObject *param_2,
                                       int param_3, void *param_4, char param_5);
struct c_sil9030 *CSIL9030_Constructor(struct c_sil9030 *param_1, struct CObject *param_2,
                                       int param_3, void *param_4, char param_5);
struct c_sil9034 *CSIL9034_Constructor(struct c_sil9034 *param_1, struct CObject *param_2,
                                       int param_3, void *param_4, char param_5);
struct c_adi9889 *CADI9889_Constructor(struct c_adi9889 *param_1, struct CObject *param_2,
                                       int param_3, void *param_4, char param_5);

/* Audio codec constructors */
struct c_fm31 *CFM31_Constructor(struct c_fm31 *param_1, struct CObject *param_2,
                                 int param_3, void *param_4, void *param_5,
                                 void *param_6, char param_7);

struct c985_poc *CQLCodec_Constructor(struct c985_poc *d, struct CObject *param_2,
                                      u32 param_3, void *param_4, void *param_5);

/* CQLCodec interface function stubs - change all to c985_poc */
int CQLCodec_InitDevice(struct pci_dev *pdev, const struct pci_device_id *id);
int CQLCodec_Release(struct c985_poc *param_1);
int CQLCodec_Reset(struct c985_poc *param_1);
int CQLCodec_Set(struct c985_poc *param_1, u32 param_2, void *param_3);
int CQLCodec_Get(struct c985_poc *param_1, u32 param_2, void *param_3);
int CQLCodec_AllocEncodeTask(struct c985_poc *param_1, u32 *param_2);
int CQLCodec_AllocDecodeTask(struct c985_poc *param_1, u32 *param_2);
int CQLCodec_ReleaseTask(struct c985_poc *param_1, u32 param_2);
_EQPErrors CQLCodec_Open(struct IMpegCodec *param_1, u32 param_2, u32 param_3,
                  void *param_4, u32 *param_5,
                  void *param_6, void *param_7);
_EQPErrors CQLCodec_Close(struct IMpegCodec *iface, u32 handle);
int CQLCodec_Start(struct c985_poc *param_1, u32 param_2);
_EQPErrors CQLCodec_Stop(struct IMpegCodec *this, u32 hStream);
int CQLCodec_Acquire(struct c985_poc *param_1, u32 param_2);
int CQLCodec_Pause(struct c985_poc *param_1, u32 param_2);
int CQLCodec_Step(struct c985_poc *param_1, u32 param_2);
int CQLCodec_SetRate(struct c985_poc *param_1, u32 param_2, u32 param_3);
int CQLCodec_GetRate(struct c985_poc *param_1, u32 param_2, u32 *param_3);
int CQLCodec_BeginFlush(struct c985_poc *param_1, u32 param_2);
int CQLCodec_Flush(struct c985_poc *param_1, u32 param_2);
int CQLCodec_EndFlush(struct c985_poc *param_1, u32 param_2);

int CQLCodec_AddBuffer(struct IMpegCodec *param_1, u32 param_2,
                       struct _QP_BUFFER_DESCRIPTOR *param_3);

int CQLCodec_CancelBuffer(struct c985_poc *param_1, u32 param_2);
int CQLCodec_TimeoutBuffer(struct c985_poc *param_1, u32 param_2);
int CQLCodec_GetTime(struct c985_poc *param_1, u32 param_2, u64 *param_3);
int CQLCodec_XferData(struct c985_poc *param_1, u32 param_2, void *param_3, u32 param_4);

/* CQLCodec destructor */
void CQLCodec_Destructor(struct c985_poc *param_1);

struct c_i2c *CI2C_Constructor(struct c_i2c *param_1, struct CObject *param_2, int param_3,
                               enum qpi2c_type param_4, u32 param_5, u32 param_6,
                               void *param_7, struct c985_poc *param_8);  /* Changed */
int CQLCodec_ReleaseTask(struct c985_poc *param_1, u32 param_2);
int CQLCodec_IsCodecIdle(struct c985_poc *param_1);
int CQLCodec_IsCodecError(struct cql_codec *codec);
void CQLCodec_ClrCodecError(struct cql_codec *param_1);
int CQLCodecLibBusCallback(void *context, u32 event_type, void *param);
int CQLCodec_RegisterISR(struct c985_poc *d);

#endif /* CQLCODEC_H */
