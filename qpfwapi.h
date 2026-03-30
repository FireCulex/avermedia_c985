/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPFWAPI_H
#define QPFWAPI_H

#include <linux/types.h>

struct c985_poc;

#define REG_MSG_STATUS      0x6CC
#define REG_MSG_DATA        0x6FC
#define REG_ARM_RESPONSE    0x6C8

/* HCI registers */
#define REG_HCI_INT_MASK    0x800
#define REG_HCI_INT_STATUS  0x804
#define REG_HCI_ARM_STATUS  0x80C

/* Interrupt bits */
#define HCI_INT_MESSAGE     BIT(16)
#define HCI_INT_DMA_READ    BIT(17)
#define HCI_INT_DMA_WRITE   BIT(18)
#define HCI_INT_ALL         0x70000

int  qpfwapi_mailbox_ready(struct c985_poc *d, unsigned int timeout_ms);
void qpfwapi_mailbox_done(struct c985_poc *d);
int  qpfwapi_send_message(struct c985_poc *d, u32 task_id, u32 message);
void qpfwapi_ack_arm_message(struct c985_poc *d);
int  qpfwapi_get_arm_message(struct c985_poc *d,
                             u32 *msg, u32 *status,
                             u32 *p1, u32 *p2, u32 *p3, u32 *p4);

#endif
