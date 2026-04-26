// SPDX-License-Identifier: GPL-2.0
/*
 * qpfwapi.c - AVerMedia C985 Firmware Command Interface
 *
 * This implements the register-based mailbox protocol used by the C985 firmware.
 * Function names follow the Windows driver naming convention:
 *   - QPFWAPI_*       = Low-level firmware API
 *   - QPFWCODECAPI_*  = High-level codec API (calls QPFWAPI_*)
 *
 * Architecture:
 * - BAR0[0x04] doorbell is used ONCE at boot to wake ARM from WFI
 * - After boot, firmware polls BAR1 registers for commands
 * - Commands use BAR1 registers: 0x6F8 (param), 0x6FC (message), 0x6CC (status)
 * - Firmware clears bit 0 of 0x6CC when command is consumed
 */

#include <linux/io.h>
#include <linux/delay.h>
#include "structs.h"
#include "avermedia_c985.h"
#include "qpfwapi.h"

/*
 * QPFWAPI_MailboxReady - Wait for mailbox to be ready
 * @d: device structure
 * @timeout_ms: timeout in milliseconds
 *
 * Returns: 0 if ready, -ETIMEDOUT if firmware didn't clear busy flag
 */
int QPFWAPI_MailboxReady(struct c985_poc *d, u32 timeout_ms)
{
    u32 val;
    unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);

    do {
        val = readl(c985_bar1(d) + REG_TO_ARM_MSG_STATUS);
        if (!(val & 1))  /* bit 0 = 0 means READY */
            return 0;
        usleep_range(100, 200);
    } while (time_before(jiffies, deadline));

    dev_err(&d->pdev->dev, "QPFWAPI_MailboxReady timeout: status=0x%08x\n", val);
    return -ETIMEDOUT;
}

/*
 * QPFWAPI_MailboxDone - Release mailbox after command
 * @d: device structure
 *
 * Windows driver releases a semaphore here. For Linux, this is a no-op
 * but kept for API completeness.
 */
void QPFWAPI_MailboxDone(struct c985_poc *d)
{
    /* No-op in Linux - no semaphore needed */
}

/*
 * QPFWAPI_SendMessageToARM - Send a message to the ARM processor
 * @d: device structure
 * @task_id: task ID (typically 8 for HCI thread)
 * @msg: ARM message structure
 * @has_response: non-zero if expecting response status
 * @status: receives response status (can be NULL if has_response is 0)
 * @timeout_ms: timeout for mailbox ready (0 = skip mailbox wait)
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWAPI_SendMessageToARM(struct c985_poc *d, u32 task_id,
                             struct arm_message *msg, int has_response,
                             struct host_message_status *status, u32 timeout_ms)
{
    u32 status_word;
    u32 high_bits;
    int ret;

    /* Wait for mailbox ready if timeout specified */
    if (timeout_ms != 0) {
        ret = QPFWAPI_MailboxReady(d, timeout_ms);
        if (ret < 0) {
            dev_err(&d->pdev->dev,
                    "QPFWAPI_SendMessageToARM: mailbox not ready\n");
            return ret;
        }
    }

    /* Build status word high bits based on chip type */
    if ((d->codec.m_ChipType & 0xe) == 0) {
        /* Old chip: task_id in bits [31:16] */
        high_bits = task_id << 16;
    } else {
        /* New chip (C985): use high bits from status or message */
        if (status != NULL) {
            high_bits = status->Read & 0xFFFF0000;
        } else {
            high_bits = msg->Read & 0xFFFF0000;
        }
    }

    /* Build status word:
     * Bits [31:16] = high_bits (chip-dependent)
     * Bits [15:8]  = flags (bit 8 = has_response)
     * Bit  [0]     = trigger bit (always 1)
     */
    status_word = high_bits | ((has_response ? 1 : 0) << 8) | 1;

    /* Write status/trigger to 0x6CC */
    writel(status_word, c985_bar1(d) + REG_TO_ARM_MSG_STATUS);
    wmb();

    /* Write full message to 0x6FC */
    writel(msg->Read, c985_bar1(d) + REG_TO_ARM_MESSAGE);
    wmb();

    dev_dbg(&d->pdev->dev, "QPFWAPI_SendMessageToARM: task=%u status_word=0x%08x msg=0x%08x\n",
            task_id, status_word, msg->Read);

    /* Release mailbox if timeout was specified */
    if (timeout_ms != 0) {
        QPFWAPI_MailboxDone(d);
    }

    return 0;
}

/*
 * QPFWAPI_GetARMMessage - Read response from ARM processor
 * @d: device structure
 * @msg: receives ARM response message (0x6B0)
 * @status: receives ARM response status (0x6C8)
 * @param0-4: receives response parameters (0x6B4-0x6C4)
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWAPI_GetARMMessage(struct c985_poc *d, struct host_message *msg,
                          struct host_message_status *status,
                          u32 *param0, u32 *param1, u32 *param2,
                          u32 *param3, u32 *param4)
{
    msg->Read    = readl(c985_bar1(d) + REG_FROM_ARM_MESSAGE);
    status->Read = readl(c985_bar1(d) + REG_FROM_ARM_MSG_STATUS);
    *param0      = readl(c985_bar1(d) + REG_FROM_ARM_PARAM0);
    *param1      = readl(c985_bar1(d) + REG_FROM_ARM_PARAM1);
    *param2      = readl(c985_bar1(d) + REG_FROM_ARM_PARAM2);
    *param3      = readl(c985_bar1(d) + REG_FROM_ARM_PARAM3);
    *param4      = readl(c985_bar1(d) + REG_FROM_ARM_PARAM4);

    return 0;
}

/*
 * QPFWAPI_Init - Initialize firmware API
 * @d: device structure
 *
 * Call this after ARM has booted (after doorbell ring + delay)
 */
int QPFWAPI_Init(struct c985_poc *d)
{
    u32 status;

    dev_dbg(&d->pdev->dev, "QPFWAPI_Init: Initializing firmware interface\n");

    /* Clear any stale mailbox state */
    writel(0x00000000, c985_bar1(d) + REG_TO_ARM_MSG_STATUS);
    wmb();

    /* Wait a bit for firmware to settle */
    msleep(100);

    /* Verify mailbox is ready */
    status = readl(c985_bar1(d) + REG_TO_ARM_MSG_STATUS);
    if (status & 1) {
        dev_warn(&d->pdev->dev,
                 "QPFWAPI_Init: Mailbox busy at init (0x%08x)\n", status);
        return -EBUSY;
    }

    dev_dbg(&d->pdev->dev, "QPFWAPI_Init: Ready (status=0x%08x)\n", status);
    return 0;
}

/*
 * arm_ring_doorbell - Ring the ARM boot doorbell
 * @d: device structure
 *
 * This wakes the ARM from WFI state and should only be called ONCE
 * during initialization, not for every command.
 */
void arm_ring_doorbell(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "Ringing ARM doorbell\n");

    iowrite32(0x00, c985_bar0(d) + 0x04);
    wmb();
    udelay(100);

    iowrite32(0x01, c985_bar0(d) + 0x04);
    wmb();
}

/*
 * QPFWAPI_AckARMMessage - Acknowledge ARM message
 * @d: device structure
 * @msg: host message received from ARM
 * @status: host message status
 * @ack: non-zero to send acknowledgment with timeout
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWAPI_AckARMMessage(struct c985_poc *d, struct host_message *msg,
                          struct host_message_status *status, int ack)
{
    int ret;
    struct arm_message arm_msg;
    u32 task_id;
    u32 cmd;
    u32 timeout;

    /* Check if response is needed (bit 8 of status) */
    if (((status->Read >> 8) & 1) == 1) {
        cmd = msg->Read & 0xffff;

        if (d->codec.m_ChipType == 1) {
            /* Old chip type */
            arm_msg.Read = (cmd < 0x80) ? 0x31 : 0xa2;
            task_id = status->Read >> 16;
        } else {
            /* New chip type (C985) */
            arm_msg.Read = (cmd < 0x80) ? 0x31 : 0xa2;
            arm_msg.Read |= (msg->Read & 0xffff0000);
            task_id = msg->Read >> 16;
        }

        timeout = (ack == 0) ? 0 : 1000;

        ret = QPFWAPI_SendMessageToARM(d, task_id, &arm_msg, 0, status, timeout);
        if (ret < 0) {
            dev_err(&d->pdev->dev,
                    "QPFWAPI_AckARMMessage() failed %d\n", ret);
            return ret;
        }
    }

    /* Clear the pending bit */
    status->Read &= 0xfffffffe;

    /* Write back to register */
    writel(status->Read, c985_bar1(d) + REG_FROM_ARM_MSG_STATUS);

    return 0;
}
