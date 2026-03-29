// SPDX-License-Identifier: GPL-2.0
// qpfwapi.c — ARM firmware mailbox API for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "qpfwapi.h"

#define REG_MSG_STATUS  0x6CC
#define REG_MSG_DATA    0x6FC

/* Wait for mailbox to be ready (bit 0 of 0x6CC cleared) */
int qpfwapi_mailbox_ready(struct c985_poc *d, unsigned int timeout_ms)
{
    unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);
    u32 val;

    for (;;) {
        val = readl(d->bar1 + 0x6CC);
        if ((val & 1) == 0)
            return 0;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "FWAPI: mailbox timeout (0x%08x)\n", val);
            return -ETIMEDOUT;
        }
        usleep_range(100, 200);
    }
}
/* Release mailbox */
void qpfwapi_mailbox_done(struct c985_poc *d)
{
    /* Nothing to do for PCIe - just a placeholder */
}

/* Send message to ARM firmware */
int qpfwapi_send_message(struct c985_poc *d, u32 task_id, u32 message)
{
    u32 status, reg24, val;
    unsigned long timeout;
    int ret;

    /* Wait for mailbox ready */
    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    status = (message & 0xffff0000) | 1;

    dev_dbg(&d->pdev->dev, "FWAPI: send msg status=0x%08x data=0x%08x\n",
            status, message);

    /* Write message */
    writel(status, d->bar1 + 0x6CC);
    writel(message, d->bar1 + 0x6FC);

    /* Trigger interrupt to ARM */
    reg24 = readl(d->bar1 + 0x24);
    if (!(reg24 & 0x2000000))
        writel(0x2000000, d->bar1 + 0x24);

    /* Wait for ARM to consume (bit 0 of 0x6CC clears) */
    timeout = jiffies + msecs_to_jiffies(500);
    for (;;) {
        val = readl(d->bar1 + 0x6CC);
        if ((val & 1) == 0)
            break;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "FWAPI: ARM didn't consume msg (0x%08x)\n", val);
            return -ETIMEDOUT;
        }
        usleep_range(100, 200);
    }

    /* Clear ARM response interrupt */
    writel(0x40000000, d->bar1 + 0x4030);

    return 0;
}
