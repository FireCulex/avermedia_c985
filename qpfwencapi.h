/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWENCAPI_H
#define QPFWENCAPI_H

#include <linux/types.h>

struct c985_poc;

int qpfwencapi_start(struct c985_poc *d);
int qpfwencapi_stop(struct c985_poc *d);

int qpfwencapi_system_open(struct c985_poc *d, u32 task_id, u32 function);
int qpfwencapi_system_link(struct c985_poc *d, u32 task_id,
                           u32 vi, u32 vic, u32 vo, u32 voc,
                           u32 ai, u32 aic, u32 ao, u32 aoc);
int qpfwencapi_set_viu_sync_code(struct c985_poc *d, u32 task_id,
                                 u32 code1, u32 code2);
int qpfwencapi_update_config(struct c985_poc *d, u32 task_id);

#endif
