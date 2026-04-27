/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _C985_CHANNEL_H
#define _C985_CHANNEL_H

#include "structs.h"
#include "qperrors.h"
#include "include/abi/qp_buffer_descriptor.h"
#include "include/abi/cchannel.h"


/* CChannel - from Ghidra */
struct c_channel *CChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                       u32 param_3, u32 param_4, int param_5, int param_6,
                                       enum channel_direction param_7, u32 param_8,
                                       struct c_task *param_9);
int CChannel_Open(struct c_channel *param_1, u32 param_2, u32 param_3, void *param_4,
                  void *param_5, void *param_6);
int CChannel_Close(struct c_channel *param_1);

/* CYUVInChannel - from Ghidra */
struct c_channel *CYUVInChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                            u32 param_3, u32 param_4, struct c_task *param_5);
int CYUVInChannel_Open(struct c_channel *param_1, u32 param_2, u32 param_3,
                       void *param_4, void *param_5, void *param_6);

/* CPCMInChannel - from Ghidra */
struct c_channel *CPCMInChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                            u32 param_3, u32 param_4, struct c_task *param_5);

/* Stubs - need Ghidra decompiles */
void CChannel_Destructor(struct c_channel *param_1);
struct c_channel *CAESOutChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                             u32 param_3, u32 param_4, struct c_task *param_5);
int CMPEGOutChannel_CompleteBuffer(struct c_channel *channel, u32 bytes_used);


int CMPEGOutChannel_GetBuffer(struct c_channel *channel,
                              struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                              u8 **buffer,
                              u32 *size);

struct c_channel *CMPEGOutChannel_Constructor(struct c_channel *param_1, struct CObject *param_2,
                                              u32 param_3, u32 param_4, struct c_task *param_5);

int CMPEGOutChannel_Start(struct c_channel *channel);
int CMPEGOutChannel_Stop(struct c_channel *channel);
int CMPEGOutChannel_Pause(struct c_channel *channel);
int CMPEGOutChannel_GetBuffer(struct c_channel *channel, struct _QP_BUFFER_DESCRIPTOR **buf_desc,
                              u8 **buffer, u32 *size);
int CMPEGOutChannel_CompleteBuffer(struct c_channel *channel, u32 bytes_used);
int CChannel_needByteSwapping(struct c_channel *ch);
_EQPErrors CChannel_DeviceCallback(struct c_channel *channel, u32 param, void *data);
/* int CVBIOutChannel_GetVBIFormat(struct c_vbi_out_channel *chan, struct qp_vbi_dataformat *fmt); */

#endif /* _C985_CHANNEL_H */
