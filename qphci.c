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

// SPDX-License-Identifier: GPL-2.0
// qphci.c — QPHCI initialization for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "qphci.h"
#include "cpr.h"

/*
 * QPHCI_PowerUp - power up sequence
 * Need to find this function in decompile, but for now placeholder
 */
static int qphci_power_up(struct c985_poc *d)
{
    u32 pad_ctl;

    dev_info(&d->pdev->dev, "QPHCI_PowerUp()\n");

    /* Read pad control register 0x50 */
    pad_ctl = readl(d->bar1 + 0x50);

    /* Clear bit 8 */
    pad_ctl &= 0xFFFFFEFF;

    /* Write back */
    writel(pad_ctl, d->bar1 + 0x50);
    dev_info(&d->pdev->dev, "QPHCI_PowerUp() pad control = 0x%x\n", pad_ctl);

    /* Write 0xf00 to DDR control register */
    writel(0x00000F00, d->bar1 + 0x0F1C);

    /* Reset ARM (halt) */
    return dm_reset_arm(d, 0);
}

/*
 * QPHCI_Init - main initialization matching Windows driver
 *
 * For QPHCI_BUS_PCI with chip_ver == 0x10020:
 *   - Uses CPR_MemoryRead/CPR_MemoryWrite
 *   - Uses DM_ResetArm
 *   - Calls QPHCI_PowerUp
 *   - Sets up 3 memory mapping windows
 */
int qphci_init(struct c985_poc *d)
{
    int i, ret;
    u32 page_size = QPHCI_PAGE_SIZE;

    dev_info(&d->pdev->dev, "QPHCI_Init()\n");

    /* Read chip version */
    d->chip_ver = readl(d->bar1 + 0x38);
    dev_info(&d->pdev->dev, "QPHCI_Init() chip_ver=0x%08x\n", d->chip_ver);

    /* Call QPHCI_PowerUp */
    ret = qphci_power_up(d);
    if (ret)
        return ret;

    /* Set up 3 memory mapping windows */
    for (i = 0; i < 3; i++) {
        u32 offset = i * page_size;
        u32 start = offset + 0x4000;
        u32 end = start + page_size - 1;
        u32 base = i * 0x0c;

        writel(start,  d->bar1 + 0x81c + base);
        writel(end,    d->bar1 + 0x820 + base);
        writel(offset, d->bar1 + 0x824 + base);
    }

    /* Magic control writes to enable HCI engine */
    writel(0x70003124, d->bar1 + 0x840);
    writel(0x90003124, d->bar1 + 0x840);

    dev_info(&d->pdev->dev, "QPHCI_Init() done\n");
    return 0;
}
// In qphci.c

/*
 * DM_ResetArm - halt or start the ARM processor
 * param run: 0 = halt, 1 = start
 */
int dm_reset_arm(struct c985_poc *d, int run)
{
    u32 val;
    unsigned long timeout;

    dev_info(&d->pdev->dev, "DM_ResetArm(run=%d)\n", run);

    if (run == 0) {
        val = readl(d->bar1 + 0x00);
        dev_info(&d->pdev->dev, "DM_ResetArm: reg 0x00 = 0x%08x (preserving)\n", val);

        /* Do NOT write 0 to reg 0x00 - it kills DDR clocks
         * The Windows driver does this but never does rmmod/insmod */

        writel(0x00000000, d->bar1 + 0x80C);
        writel(0x00000001, d->bar1 + 0x800);
        writel(0x00000001, d->bar1 + 0x10);

        val = readl(d->bar1 + 0x1C);
        val += 0xFFFF;
        writel(val, d->bar1 + 0x18);

        writel(0x00000108, d->bar1 + 0x10);

        msleep(15);

        timeout = jiffies + msecs_to_jiffies(3000);
        do {
            val = readl(d->bar1 + 0x800);
            if (val == 0)
                break;
            if (time_after(jiffies, timeout)) {
                dev_err(&d->pdev->dev, "DM_ResetArm() FAILED! reg 0x800 = 0x%x\n", val);
                return -ETIMEDOUT;
            }
            udelay(10);
        } while (1);

        writel(0x00000000, d->bar1 + 0x10);

        dev_info(&d->pdev->dev, "DM_ResetArm: ARM halted OK\n");
    }

    /* Common to both halt and start */
    writel(0x00000000, d->bar1 + 0x6CC);

    if (run == 0) {
        writel(0x00000000, d->bar1 + 0x80C);
    } else {
        writel(0x00000001, d->bar1 + 0x80C);
    }

    dev_info(&d->pdev->dev, "DM_ResetArm(run=%d) done\n", run);
    return 0;
}
