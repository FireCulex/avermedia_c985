/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWENCAPI_H
#define QPFWENCAPI_H

#include "qperrors.h"

/* Command IDs */
#define ARM_MSG_SYSTEM_OPEN     0xF1
#define ARM_MSG_SYSTEM_LINK     0xF2
#define ARM_MSG_SYSTEM_CLOSE    0xF3

/*
 * QPFWCODECAPI - High-level codec API
 */

int QPFWCODECAPI_SystemOpen(struct cql_codec *codec, u32 task_id, u32 function);
int QPFWCODECAPI_SystemClose(struct cql_codec *codec, u32 task_id);
int QPFWCODECAPI_SystemLink(struct cql_codec *codec, u32 task_id,
                            u32 video_input, u32 video_in_ch,
                            u32 video_output, u32 video_out_ch,
                            u32 audio_input, u32 audio_in_ch,
                            u32 audio_output, u32 audio_out_ch);
int QPFWENCAPI_StartEncoder(struct c985_poc *d, u32 task_id);
int QPFWENCAPI_StopEncoder(struct c985_poc *d, u32 task_id,
                           int bStopAtGOP, u32 channel_dataType);
_EQPErrors QPFWENCAPI_SetViuSyncCode(struct cql_codec *codec, u32 task_id, u32 sync_code_1, u32 sync_code_2);

int QPFWENCAPI_UpdateConfig(struct c985_poc *d, u32 task_id);
/* Encoder register set functions */
int QPFWENCAPI_SetSystemControl(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetPictureResolution(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetInputControl(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetRateControl(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetVBRBitRate(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetFilterControl(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetGOPLoopFilter(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetETControl(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetBlockSize(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetOutPictureResolution(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetAudioControlParameters(struct c985_poc *d, u32 value);
int QPFWENCAPI_SetAudioControlExtension(struct c985_poc *d, u32 value);

// Add these declarations
_EQPErrors QPFWENCAPI_SetEncMode(struct cql_codec *codec, u32 task_id, u32 capMode, u32 trigMode, u32 gpioPin);
_EQPErrors QPFWENCAPI_SetMJPEGQuality(struct cql_codec *codec, u32 task_id, u32 quality);
_EQPErrors QPFWENCAPI_SetMJPEGFrameBuffer(struct cql_codec *codec, u32 task_id, u32 framebuffer);
_EQPErrors QPFWENCAPI_SetExternalTriggerToSync(struct cql_codec *codec, u32 task_id, u32 enable, u32 gpioPin);
_EQPErrors QPFWENCAPI_SetPTSResetByTrigger(struct cql_codec *codec, u32 task_id, u32 enable, u32 gpioPin, u32 immediate);
_EQPErrors QPFWENCAPI_SetRawVideoDecimation(struct cql_codec *codec, u32 task_id, u32 input_fmt, u32 output_fmt, u32 scale);
_EQPErrors QPFWENCAPI_SetDeinterlaceMode(struct cql_codec *codec, u32 task_id, u32 mode);
_EQPErrors QPFWENCAPI_SetRateControlEx(struct cql_codec *codec, u32 task_id, u32 param1, u32 param2, u32 param3);
_EQPErrors QPFWENCAPI_SetLowBitrateMode(struct cql_codec *codec, u32 task_id, u32 enable);
_EQPErrors QPFWENCAPI_SetIndexCapture(struct cql_codec *codec, u32 task_id, u32 freq);
_EQPErrors QPFWENCAPI_SetVBIInfo(struct cql_codec *codec, u32 task_id, u32 enable, u32 top_start, u32 top_count,
                                 u32 top_pixel, u32 top_samples, u32 bot_start, u32 bot_count,
                                 u32 bot_pixel, u32 bot_samples);
_EQPErrors QPFWENCAPI_SetMP4VideoBlockNumber(struct cql_codec *codec, u32 task_id, u32 blocknum);
_EQPErrors QPFWENCAPI_SetAudioEnhancement(struct cql_codec *codec, u32 task_id, u32 gain1, u32 gain2,
                                          u32 add, u32 sub, u32 att1, u32 att2, u32 lgain, u32 rgain);
_EQPErrors QPFWENCAPI_EnableVidPadding(struct cql_codec *codec, u32 task_id, u32 enable);
_EQPErrors QPFWENCAPI_StillVideoInput(struct cql_codec *codec, u32 task_id, u32 enable);
_EQPErrors QPFWENCAPI_SetLargeCompressBufferControl(struct cql_codec *codec, u32 task_id, u32 value);

/* VBI function signature matching channel.h expectation */
/* _EQPErrors CVBIOutChannel_GetVBIFormat(struct c_vbi_out_channel *chan, struct qp_vbi_dataformat *fmt); */

#endif /* QPFWENCAPI_H */



