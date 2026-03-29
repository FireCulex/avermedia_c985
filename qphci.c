// SPDX-License-Identifier: GPL-2.0
// qphci.c — QPHCI memory window and ARM loop initialization

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "qphci.h"
#include "cpr.h"

/*
 * QPHCI_ReInit — re-programs the three HCI memory mapping windows and
 * issues the two magic control writes to 0x840 to re-enable the HCI engine.
 */
int qphci_reinit(struct c985_poc *d)
{
    u32 offset, start, end;
    int i;

    dev_info(&d->pdev->dev, "QPHCI: reinit\n");

    for (i = 0; i < 3; i++) {
        offset = i * QPHCI_PAGE_SIZE;
        start = offset + 0x4000;
        end = start + QPHCI_PAGE_SIZE - 1;

        writel(start,  d->bar1 + REG_MEM_WIN_BASE + i * 0x0c + 0x00);
        writel(end,    d->bar1 + REG_MEM_WIN_BASE + i * 0x0c + 0x04);
        writel(offset, d->bar1 + REG_MEM_WIN_BASE + i * 0x0c + 0x08);
    }

    writel(0x70003124, d->bar1 + REG_MEM_CTL);
    writel(0x90003124, d->bar1 + REG_MEM_CTL);

    dev_info(&d->pdev->dev, "QPHCI: reinit done\n");
    return 0;
}

/*
 * QPHCI_InitArmLoop — installs 0xEAFFFFFE (ARM branch-to-self) at card RAM
 * words 0..7 via CPR to park the ARM at the reset vector before firmware boot.
 */
int qphci_init_arm_loop(struct c985_poc *d)
{
    int i;

    dev_info(&d->pdev->dev, "QPHCI: init ARM loop\n");

    writel(0x00000000, d->bar1 + REG_ARM_BOOT);

    for (i = 0; i < 8; i++) {
        u32 addr_field = (i << 2) & 0x1ffffffc;
        u32 ctl, tmp, status, done_code;
        unsigned long timeout;

        done_code = (d->chip_ver == CPR_CHIPVER_SPECIAL) ? 0x00 : 0x42;

        writel(addr_field, d->bar1 + REG_CPR_WR_ADDR);

        ctl = readl(d->bar1 + REG_CPR_WR_CTL);
        ctl &= 0xffff0003;
        ctl |= 0x4;
        writel(ctl, d->bar1 + REG_CPR_WR_CTL);
        writel(0xeafffffe, d->bar1 + REG_CPR_WR_DATA);

        timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
        for (;;) {
            tmp = readl(d->bar1 + REG_CPR_WR_CTL);
            status = (tmp >> 18) & 0xff;
            if (status == done_code)
                break;
            if (time_after(jiffies, timeout)) {
                dev_err(&d->pdev->dev, "QPHCI: CPR write timeout at word %d\n", i);
                return -ETIMEDOUT;
            }
            udelay(10);
        }
    }

    dev_info(&d->pdev->dev, "QPHCI: init ARM loop done\n");
    return 0;
}
