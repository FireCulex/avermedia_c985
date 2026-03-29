/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWENCAPI_H
#define QPFWENCAPI_H

struct c985_poc;

int qpfwencapi_system_link(struct c985_poc *d, u32 task_id,
                           u32 vid_in, u32 vid_in_ch,
                           u32 vid_out, u32 vid_out_ch,
                           u32 aud_in, u32 aud_in_ch,
                           u32 aud_out, u32 aud_out_ch);
int qpfwencapi_update_encoder_config(struct c985_poc *d, u32 task_id);
int qpfwencapi_start_encoder(struct c985_poc *d, u32 task_id);
int qpfwencapi_start(struct c985_poc *d);
int qpfwencapi_stop(struct c985_poc *d);
int qpfwencapi_system_open(struct c985_poc *d, u32 task_id, u32 function);
int qpfwencapi_set_viu_sync_code(struct c985_poc *d, u32 task_id, u32 code1, u32 code2);
int qpfwencapi_update_config(struct c985_poc *d, u32 task_id);
#endif
