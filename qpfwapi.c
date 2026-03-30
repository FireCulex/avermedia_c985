// SPDX-License-Identifier: GPL-2.0
/*
 * qpfwapi.c - Low-level mailbox communication with ARM firmware
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "avermedia_c985.h"
#include "qpfwapi.h"

/*
 * Wait for mailbox to be ready (previous message consumed).
 */
int qpfwapi_mailbox_ready(struct c985_poc *d, unsigned int timeout_ms)
{
    unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);
    u32 val;

    for (;;) {
        val = readl(d->bar1 + REG_MSG_STATUS);
        if ((val & 1) == 0)
            return 0;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev,
                    "FWAPI: mailbox timeout (0x6CC=0x%08x)\n", val);
            dev_err(&d->pdev->dev,
                    "FWAPI: 0x6FC=0x%08x 0x6C8=0x%08x 0x24=0x%08x\n",
                    readl(d->bar1 + REG_MSG_DATA),
                    readl(d->bar1 + REG_ARM_RESPONSE),
                    readl(d->bar1 + 0x24));
            dev_err(&d->pdev->dev,
                    "FWAPI: 0x800=0x%08x 0x804=0x%08x 0x80C=0x%08x\n",
                    readl(d->bar1 + REG_HCI_INT_MASK),
                    readl(d->bar1 + REG_HCI_INT_STATUS),
                    readl(d->bar1 + REG_HCI_ARM_STATUS));
            return -ETIMEDOUT;
        }
        usleep_range(100, 200);
    }
}

void qpfwapi_mailbox_done(struct c985_poc *d)
{
    /* Windows driver releases semaphore here */
}

/*
 * ACK ARM response message - clear 0x6C8
 */
void qpfwapi_ack_arm_message(struct c985_poc *d)
{
    u32 status = readl(d->bar1 + REG_ARM_RESPONSE);

    if (status & 1) {
        dev_dbg(&d->pdev->dev, "FWAPI: ACK ARM message 0x%08x\n", status);
        /* Clear bit 0 to acknowledge */
        writel(status & ~1, d->bar1 + REG_ARM_RESPONSE);
    }
}

/*
 * Send message to ARM
 *
 * The ARM interrupt is triggered via reg 0x24 bit 25. But the ARM
 * also needs to have its HCI interrupt handler enabled. Looking at
 * the diagnostic: 0x24=0x02000000 still set after timeout means
 * the ARM never cleared it, implying:
 *   1. ARM interrupt handler not running, OR
 *   2. ARM not configured to respond to this interrupt, OR
 *   3. There's a different trigger mechanism
 *
 * Windows PCI_EnableInterrupt likely sets up the ARM side.
 */
int qpfwapi_send_message(struct c985_poc *d, u32 task_id, u32 message)
{
    u32 status_word;
    u32 reg24;
    int ret;

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    /* Clear any pending ARM response first */
    qpfwapi_ack_arm_message(d);

    /*
     * Build status word:
     *   bits [31:16] = task_id
     *   bit  [8]     = 0 (not ACK)
     *   bit  [0]     = 1 (message pending)
     */
    status_word = (message & 0xffff0000) | 1;

    dev_dbg(&d->pdev->dev,
            "FWAPI: send 0x6CC=0x%08x 0x6FC=0x%08x\n",
            status_word, message);

    /*
     * Write order from Windows driver:
     * 1. Status to 0x6CC
     * 2. Message to 0x6FC
     * 3. Trigger interrupt
     */
    writel(status_word, d->bar1 + REG_MSG_STATUS);
    writel(message, d->bar1 + REG_MSG_DATA);
    wmb();

    /*
     * Trigger ARM interrupt via reg 0x24 bit 25.
     *
     * PCI_SetInterrupt checks if already set before writing.
     * But since ARM isn't clearing it, try unconditional write.
     */
    reg24 = readl(d->bar1 + 0x24);

    /* Try clearing first, then setting (edge trigger) */
    if (reg24 & 0x2000000) {
        writel(reg24 & ~0x2000000, d->bar1 + 0x24);
        wmb();
        udelay(10);
    }

    /* Now set the interrupt */
    writel(0x2000000, d->bar1 + 0x24);
    wmb();

    /* Wait for ARM to consume */
    ret = qpfwapi_mailbox_ready(d, 2000);
    if (ret) {
        dev_err(&d->pdev->dev,
                "FWAPI: ARM didn't consume msg 0x%08x\n", message);
        return ret;
    }

    return 0;
}

/*
 * Read ARM message registers.
 */
int qpfwapi_get_arm_message(struct c985_poc *d,
                            u32 *msg, u32 *status,
                            u32 *p1, u32 *p2, u32 *p3, u32 *p4)
{
    if (msg)    *msg    = readl(d->bar1 + 0x6B0);
    if (status) *status = readl(d->bar1 + REG_ARM_RESPONSE);
    if (p1)     *p1     = readl(d->bar1 + 0x6B4);
    if (p2)     *p2     = readl(d->bar1 + 0x6B8);
    if (p3)     *p3     = readl(d->bar1 + 0x6BC);
    if (p4)     *p4     = readl(d->bar1 + 0x6C0);
    return 0;
}
