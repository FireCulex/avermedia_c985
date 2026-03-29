/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWAPI_H
#define QPFWAPI_H

struct c985_poc;

int  qpfwapi_mailbox_ready(struct c985_poc *d, unsigned int timeout_ms);
void qpfwapi_mailbox_done(struct c985_poc *d);
int  qpfwapi_send_message(struct c985_poc *d, u32 task_id, u32 message);

#endif
