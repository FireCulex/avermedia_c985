/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWENCAPI_H
#define QPFWENCAPI_H

/* Command IDs */
#define ARM_MSG_SYSTEM_OPEN     0xF1
#define ARM_MSG_SYSTEM_LINK     0xF2
#define ARM_MSG_SYSTEM_CLOSE    0xF3

/*
 * QPFWCODECAPI - High-level codec API
 */
int QPFWCODECAPI_SystemOpen(struct c985_poc *d, u32 task_id, u32 function);
int QPFWCODECAPI_SystemClose(struct c985_poc *d, u32 task_id);
int QPFWCODECAPI_SystemLink(struct c985_poc *d, u32 task_id,
                            u32 video_input, u32 video_in_ch,
                            u32 video_output, u32 video_out_ch,
                            u32 audio_input, u32 audio_in_ch,
                            u32 audio_output, u32 audio_out_ch);
#endif

