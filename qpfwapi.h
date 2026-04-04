// SPDX-License-Identifier: GPL-2.0
#ifndef QPFWAPI_H
#define QPFWAPI_H

#include <linux/types.h>

struct c985_poc;

/* BAR1 mailbox registers - Host to ARM */
#define REG_TO_ARM_PARAM        0x6F8   /* Command parameter */
#define REG_TO_ARM_MESSAGE      0x6FC   /* ARM message word */
#define REG_TO_ARM_MSG_STATUS   0x6CC   /* Status/trigger (bit 0 = busy) */

/* BAR1 mailbox registers - ARM to Host */
#define REG_FROM_ARM_MESSAGE    0x6B0   /* ARM response message */
#define REG_FROM_ARM_PARAM0     0x6B4   /* Response param 0 */
#define REG_FROM_ARM_PARAM1     0x6B8   /* Response param 1 */
#define REG_FROM_ARM_PARAM2     0x6BC   /* Response param 2 */
#define REG_FROM_ARM_PARAM3     0x6C0   /* Response param 3 */
#define REG_FROM_ARM_PARAM4     0x6C4   /* Response param 4 */
#define REG_FROM_ARM_MSG_STATUS 0x6C8   /* ARM response status */

/* Legacy names (for compatibility) */
#define REG_MSG_STATUS          REG_TO_ARM_MSG_STATUS
#define REG_MSG_DATA            REG_TO_ARM_MESSAGE
#define REG_ARM_RESPONSE        REG_FROM_ARM_MSG_STATUS

/* Command IDs */
#define ARM_MSG_SYSTEM_OPEN     0xF1
#define ARM_MSG_SYSTEM_CLOSE    0xF2

/* HCI registers */
#define REG_HCI_INT_MASK        0x800
#define REG_HCI_INT_STATUS      0x804
#define REG_HCI_ARM_STATUS      0x80C

/*
 * QPFWAPI - Low-level firmware API
 */
int QPFWAPI_Init(struct c985_poc *d);
int QPFWAPI_MailboxReady(struct c985_poc *d, int timeout_ms);
void QPFWAPI_MailboxDone(struct c985_poc *d);
int QPFWAPI_SendMessageToARM(struct c985_poc *d, u32 task_id, u32 message,
                             int has_response, int timeout_ms);
int QPFWAPI_GetARMMessage(struct c985_poc *d, u32 *message, u32 *status,
                          u32 *param0, u32 *param1, u32 *param2,
                          u32 *param3, u32 *param4);

/*
 * Boot support
 */
void arm_ring_doorbell(struct c985_poc *d);

#endif /* QPFWAPI_H */
