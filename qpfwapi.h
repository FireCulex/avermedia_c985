// SPDX-License-Identifier: GPL-2.0
#ifndef QPFWAPI_H
#define QPFWAPI_H

#include <linux/types.h>
#include <linux/mutex.h>

struct c985_poc;


/* ============================================
 * BAR1 Mailbox Registers - Host to ARM
 * ============================================ */
#define REG_TO_ARM_PARAM5       0x6E4
#define REG_TO_ARM_PARAM4       0x6E8
#define REG_TO_ARM_PARAM3       0x6EC
#define REG_TO_ARM_PARAM2       0x6F0
#define REG_TO_ARM_PARAM1       0x6F4
#define REG_TO_ARM_PARAM0       0x6F8
#define REG_TO_ARM_MESSAGE      0x6FC
#define REG_TO_ARM_MSG_STATUS   0x6CC

/* ============================================
 * BAR1 Mailbox Registers - ARM to Host
 * ============================================ */
#define REG_FROM_ARM_MESSAGE    0x6B0
#define REG_FROM_ARM_PARAM0     0x6B4
#define REG_FROM_ARM_PARAM1     0x6B8
#define REG_FROM_ARM_PARAM2     0x6BC
#define REG_FROM_ARM_PARAM3     0x6C0
#define REG_FROM_ARM_PARAM4     0x6C4
#define REG_FROM_ARM_MSG_STATUS 0x6C8

/* Legacy names */
#define REG_MSG_STATUS          REG_TO_ARM_MSG_STATUS
#define REG_MSG_DATA            REG_TO_ARM_MESSAGE
#define REG_ARM_RESPONSE        REG_FROM_ARM_MSG_STATUS

/* Command IDs */
#define ARM_MSG_SYSTEM_OPEN     0xF1

/* HCI registers */
#define REG_HCI_INT_MASK        0x800
#define REG_HCI_INT_STATUS      0x804
#define REG_HCI_ARM_STATUS      0x80C

/* ============================================
 * QPFWAPI Functions
 * ============================================ */
int QPFWAPI_Init(struct c985_poc *d);
int QPFWAPI_MailboxReady(struct c985_poc *d, u32 timeout_ms);
void QPFWAPI_MailboxDone(struct c985_poc *d);
int QPFWAPI_SendMessageToARM(struct c985_poc *d, u32 task_id,
                             struct arm_message *msg, int has_response,
                             struct host_message_status *status, u32 timeout_ms);
int QPFWAPI_GetARMMessage(struct c985_poc *d, struct host_message *msg,
                          struct host_message_status *status,
                          u32 *param0, u32 *param1, u32 *param2,
                          u32 *param3, u32 *param4);
int QPFWAPI_AckARMMessage(struct c985_poc *d, struct host_message *msg,
                          struct host_message_status *status, int ack);

/* Boot support */
void arm_ring_doorbell(struct c985_poc *d);

#endif /* QPFWAPI_H */
