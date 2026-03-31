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

/* -----------------------------------------------------------------------
 * ARM firmware commands (sent via mailbox)
 * --------------------------------------------------------------------- */

#define ARM_CMD_START           0x01
#define ARM_CMD_STOP            0x02
#define ARM_CMD_UPDATE_CONFIG   0x06
#define ARM_CMD_SET_VIU_SYNC    0x10
#define ARM_CMD_VIDEO_FRAME     0x40
#define ARM_CMD_AUDIO_FRAME     0x41
#define ARM_CMD_ERROR           0x80
#define ARM_CMD_SYSTEM_OPEN     0xF1
#define ARM_CMD_SYSTEM_LINK     0xF2

/* HCI (Host Controller Interface) interrupt control */
#define REG_HCI_INT_CTRL        0x800

/* -----------------------------------------------------------------------
 * HCI interrupt bits (for REG_HCI_INT_CTRL and REG_HCI_INT_STATUS)
 * --------------------------------------------------------------------- */

#define HCI_INT_ARM_MSG         BIT(16)   /* ARM has message for host */
#define HCI_INT_DMA_READ        BIT(17)   /* DMA read complete */
#define HCI_INT_DMA_WRITE       BIT(18)   /* DMA write complete */

int  qpfwapi_mailbox_ready(struct c985_poc *d, unsigned int timeout_ms);
void qpfwapi_mailbox_done(struct c985_poc *d);
int  qpfwapi_send_message(struct c985_poc *d, u32 task_id, u32 message);
void qpfwapi_ack_arm_message(struct c985_poc *d);
int mm_clear_interrupt(struct c985_poc *d);

int  qpfwapi_get_arm_message(struct c985_poc *d,
                             u32 *msg, u32 *status,
                             u32 *p1, u32 *p2, u32 *p3, u32 *p4);

#endif
