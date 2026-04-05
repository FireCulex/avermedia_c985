// SPDX-License-Identifier: GPL-2.0
// cpr.c — CPR (Card Processor Register) read/write for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "cpr.h"

int CPR_MemoryWrite(struct c985_poc *d, u32 param_2, u32 param_3)
{
    u32 addr_field, ctl, tmp, status;
    unsigned long timeout;
    int loop_count = 0;
    bool is_chip_v10 = (d->chip_ver == 0x10);

    addr_field = ((param_2 >> 2) & 0x7ffffff) << 2;

    dev_dbg(&d->pdev->dev, "CPR_MemoryWrite() dwAddr (0x%x) dwData(0x%x)\n",
            param_2, param_3);

    writel(addr_field, d->bar1 + 0x78c);
    ctl = readl(d->bar1 + 0x790);

    ctl &= 0xffff0003;
    ctl |= 0x4;
    writel(ctl, d->bar1 + 0x790);
    writel(param_3, d->bar1 + 0x794);

    timeout = jiffies + msecs_to_jiffies(3000);
    for (;;) {
        tmp = readl(d->bar1 + 0x790);

        if (is_chip_v10) {
            status = (tmp >> 18) & 0x3f;
            if (status == 0)
                break;
        } else {
            status = (tmp >> 18) & 0xff;
            if (status == 0x42)
                break;
        }

        loop_count++;
        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev,
                    "CPR_MemoryWrite() dwAddr (0x%x) dwData(0x%x) Timeout!\n",
                    param_2, param_3);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    return 0;
}
int CPR_MemoryRead(struct c985_poc *d, u32 param_2, u32 *param_3)
{
    u32 addr_field, ctl, tmp, status;
    unsigned long timeout;
    bool is_chip_v10 = (d->chip_ver == 0x10);
    int i;

    addr_field = ((param_2 >> 2) & 0x7ffffff) << 2;

    dev_vdbg(&d->pdev->dev, "CPR_MemoryRead() dwAddr (0x%x)\n", param_2);

    writel(addr_field, d->bar1 + 0x780);
    ctl = readl(d->bar1 + 0x784);

    ctl &= 0xffff0003;
    ctl |= 0x10;
    writel(ctl, d->bar1 + 0x784);

    timeout = jiffies + msecs_to_jiffies(3000);
    for (;;) {
        tmp = readl(d->bar1 + 0x784);
        status = (tmp >> 18) & 0x3f;

        if (is_chip_v10) {
            if (status != 0x3f && status != 0)
                break;
        } else {
            if (status != 0x3f && status != 0)  /* 0xff impossible with 0x3f mask */
                break;
        }

        if (time_after(jiffies, timeout)) {
            dev_err(&d->pdev->dev,
                    "CPR_MemoryRead() dwAddr (0x%x) Timeout!\n", param_2);
            return -ETIMEDOUT;
        }
        udelay(10);
    }

    /* Read 4 times, keep first */
    *param_3 = readl(d->bar1 + 0x788);
    for (i = 1; i < 4; i++)
        readl(d->bar1 + 0x788);

    dev_dbg(&d->pdev->dev, "CPR_MemoryRead() dwAddr (0x%x) Data(0x%x)\n",
            param_2, *param_3);

    return 0;
}

