// SPDX-License-Identifier: GPL-2.0
// cpr.c — CPR (Card Processor Register) read/write for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "cpr.h"

// CPR_MemoryWrite
int cpr_write(struct c985_poc *d, u32 card_addr, u32 val)
{
    u32 addr_field, ctl, tmp, status, done_code;
    unsigned long timeout;
    int loop_count = 0;

    done_code = (d->chip_ver == CPR_CHIPVER_SPECIAL) ? 0x00 : 0x42;
    addr_field = ((card_addr >> 2) & 0x7ffffff) << 2;

    dev_dbg(&d->pdev->dev, "CPR_WR: addr=0x%08x val=0x%08x done_code=0x%02x\n",
            card_addr, val, done_code);

    writel(addr_field, d->bar1 + REG_CPR_WR_ADDR);
    ctl = readl(d->bar1 + REG_CPR_WR_CTL);

    dev_dbg(&d->pdev->dev, "CPR_WR: initial ctl=0x%08x\n", ctl);

    ctl &= 0xffff0003;
    ctl |= 0x4;
    writel(ctl, d->bar1 + REG_CPR_WR_CTL);
    writel(val, d->bar1 + REG_CPR_WR_DATA);

    timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
    for (;;) {
        tmp = readl(d->bar1 + REG_CPR_WR_CTL);
        status = (tmp >> 18) & 0xff;

        loop_count++;
        if (loop_count <= 5 || status == done_code) {
            dev_dbg(&d->pdev->dev, "CPR_WR: loop %d status=0x%02x (want 0x%02x)\n",
                    loop_count, status, done_code);
        }

        if (status == done_code)
            break;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "CPR write timeout addr=0x%08x status=0x%02x (want 0x%02x) after %d loops\n",
                    card_addr, status, done_code, loop_count);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    dev_dbg(&d->pdev->dev, "CPR_WR: complete after %d loops\n", loop_count);
    return 0;
}

// CPR_MemoryRead
int cpr_read(struct c985_poc *d, u32 card_addr, u32 *out)
{
    u32 addr_field, ctl, tmp, status, busy_sentinel;
    unsigned long timeout;
    int loop_count = 0;

    busy_sentinel = (d->chip_ver == CPR_CHIPVER_SPECIAL) ? 0x3f : 0xff;
    addr_field = ((card_addr >> 2) & 0x7ffffff) << 2;

    dev_dbg(&d->pdev->dev, "CPR_RD: addr=0x%08x busy_sentinel=0x%02x\n",
            card_addr, busy_sentinel);

    writel(addr_field, d->bar1 + REG_CPR_RD_ADDR);
    ctl = readl(d->bar1 + REG_CPR_RD_CTL);

    dev_dbg(&d->pdev->dev, "CPR_RD: initial ctl=0x%08x\n", ctl);

    ctl &= 0xffff0003;
    ctl |= 0x10;
    writel(ctl, d->bar1 + REG_CPR_RD_CTL);

    timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
    for (;;) {
        tmp = readl(d->bar1 + REG_CPR_RD_CTL);
        status = (tmp >> 18) & 0x3f;

        loop_count++;
        if (loop_count <= 5 || (status != busy_sentinel && status != 0x00)) {
            dev_dbg(&d->pdev->dev, "CPR_RD: loop %d status=0x%02x\n",
                    loop_count, status);
        }

        if (status != busy_sentinel && status != 0x00)
            break;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "CPR read timeout addr=0x%08x status=0x%02x after %d loops\n",
                    card_addr, status, loop_count);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    *out = readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);

    dev_dbg(&d->pdev->dev, "CPR_RD: complete after %d loops, val=0x%08x\n",
            loop_count, *out);
    return 0;
}
