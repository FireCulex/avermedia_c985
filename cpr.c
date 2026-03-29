// SPDX-License-Identifier: GPL-2.0
// cpr.c — CPR (Card Processor Register) read/write for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "cpr.h"

int cpr_write(struct c985_poc *d, u32 card_addr, u32 val)
{
    u32 addr_field, ctl, tmp, status, done_code;
    unsigned long timeout;

    done_code = (d->chip_ver == CPR_CHIPVER_SPECIAL) ? 0x00 : 0x42;
    addr_field = ((card_addr >> 2) & 0x7ffffff) << 2;

    writel(addr_field, d->bar1 + REG_CPR_WR_ADDR);

    ctl = readl(d->bar1 + REG_CPR_WR_CTL);
    ctl &= 0xffff0003;
    ctl |= 0x4;
    writel(ctl, d->bar1 + REG_CPR_WR_CTL);
    writel(val, d->bar1 + REG_CPR_WR_DATA);

    timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
    for (;;) {
        tmp = readl(d->bar1 + REG_CPR_WR_CTL);
        status = (tmp >> 18) & 0xff;
        if (status == done_code)
            break;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "CPR write timeout addr=0x%08x\n", card_addr);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    return 0;
}

int cpr_read(struct c985_poc *d, u32 card_addr, u32 *out)
{
    u32 addr_field, ctl, tmp, status, busy_sentinel;
    unsigned long timeout;

    busy_sentinel = (d->chip_ver == CPR_CHIPVER_SPECIAL) ? 0x3f : 0xff;
    addr_field = ((card_addr >> 2) & 0x7ffffff) << 2;

    writel(addr_field, d->bar1 + REG_CPR_RD_ADDR);

    ctl = readl(d->bar1 + REG_CPR_RD_CTL);
    ctl &= 0xffff0003;
    ctl |= 0x10;
    writel(ctl, d->bar1 + REG_CPR_RD_CTL);

    timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
    for (;;) {
        tmp = readl(d->bar1 + REG_CPR_RD_CTL);
        status = (tmp >> 18) & 0x3f;
        if (status != busy_sentinel && status != 0x00)
            break;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev, "CPR read timeout addr=0x%08x\n", card_addr);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    *out = readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);
    readl(d->bar1 + REG_CPR_RD_DATA);

    return 0;
}
