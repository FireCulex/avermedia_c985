// SPDX-License-Identifier: GPL-2.0
// qphci.c — QPHCI memory window and ARM loop initialization

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "structs.h"
#include "avermedia_c985.h"
#include "qphci.h"
#include "cpr.h"
#include "pciecntl.h"
#include "interrupts.h"

/*
 * DM_ResetArm - halt or start the ARM processor
 * param run: 0 = halt, 1 = start
 */
int dm_reset_arm(struct c985_poc *d, int run)
{
    u32 val;
    unsigned long timeout;

    dev_dbg(&d->pdev->dev, "DM_ResetArm(run=%d)\n", run);

    if (run == 0) {
        writel(0x00000000, c985_bar1(d) + REG_ARM_CTRL);
        writel(0x00000000, c985_bar1(d) + REG_ARM_BOOT);
        writel(0x00000001, c985_bar1(d) + REG_ARM_STATUS);
        writel(0x00000001, c985_bar1(d) + REG_ARM_RESET);

        val = readl(c985_bar1(d) + REG_ARM_TIMER_CFG);
        val += 0xFFFF;
        writel(val, c985_bar1(d) + REG_ARM_TIMER_VAL);

        writel(0x00000108, c985_bar1(d) + REG_ARM_RESET);

        msleep(15);

        timeout = jiffies + msecs_to_jiffies(3000);
        do {
            val = readl(c985_bar1(d) + REG_ARM_STATUS);
            if (val == 0)
                break;
            if (time_after(jiffies, timeout)) {
                dev_err(&d->pdev->dev, "DM_ResetArm() FAILED! status=0x%x\n", val);
                return -ETIMEDOUT;
            }
            udelay(10);
        } while (1);

        writel(0x00000000, c985_bar1(d) + REG_ARM_RESET);
    }

    writel(0x00000000, c985_bar1(d) + REG_ARM_MAILBOX);

    if (run == 0) {
        writel(0x00000000, c985_bar1(d) + REG_ARM_BOOT);
    } else {
        writel(0x00000001, c985_bar1(d) + REG_ARM_BOOT);
    }

    dev_dbg(&d->pdev->dev, "DM_ResetArm(run=%d) done\n", run);
    return 0;
}

/*
 * QPHCI_PowerUp - power up sequence
 */
static int qphci_power_up(struct c985_poc *d)
{
    u32 pad_ctl;

    dev_dbg(&d->pdev->dev, "QPHCI_PowerUp()\n");

    /* Read pad control register 0x50 */
    pad_ctl = readl(c985_bar1(d) + 0x50);

    /* Clear bit 8 */
    pad_ctl &= 0xFFFFFEFF;

    /* Write back */
    writel(pad_ctl, c985_bar1(d) + 0x50);
    dev_dbg(&d->pdev->dev, "QPHCI_PowerUp() pad control = 0x%x\n", pad_ctl);

    /* Write 0xf00 to DDR control register */
    writel(0x00000F00, c985_bar1(d) + 0x0F1C);

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

    dev_dbg(&d->pdev->dev, "QPHCI_Init()\n");

    d->codec.m_hci.m_pMpegCodec = &d->codec;

    /* Read chip version */
    d->codec.m_ChipVersion = readl(c985_bar1(d) + REG_CHIP_VER);
    dev_dbg(&d->pdev->dev, "QPHCI_Init() chip_ver=0x%08x\n", d->codec.m_ChipVersion);

    d->codec.m_hci.ResetArm = dm_reset_arm;
    d->codec.m_hci.EnableInterrupts = CPCIeCntl_EnableInterrupts;
    d->codec.m_hci.DisableInterrupts = CPCIeCntl_DisableInterrupts;

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

        writel(start,  c985_bar1(d) + REG_MEM_WIN_BASE + base + 0x00);
        writel(end,    c985_bar1(d) + REG_MEM_WIN_BASE + base + 0x04);
        writel(offset, c985_bar1(d) + REG_MEM_WIN_BASE + base + 0x08);
    }

    /* Magic control writes to enable HCI engine */
    writel(0x70003124, c985_bar1(d) + REG_MEM_CTL);
    writel(0x90003124, c985_bar1(d) + REG_MEM_CTL);

    dev_dbg(&d->pdev->dev, "QPHCI_Init() done\n");
    return 0;
}

/*
 * QPHCI_ReInit — re-programs the three HCI memory mapping windows and
 * issues the two magic control writes to 0x840 to re-enable the HCI engine.
 */
int qphci_reinit(struct c985_poc *d)
{
    u32 offset, start, end;
    int i;

    dev_dbg(&d->pdev->dev, "QPHCI: reinit\n");

    for (i = 0; i < 3; i++) {
        offset = i * QPHCI_PAGE_SIZE;
        start = offset + 0x4000;
        end = start + QPHCI_PAGE_SIZE - 1;

        writel(start,  c985_bar1(d) + REG_MEM_WIN_BASE + i * 0x0c + 0x00);
        writel(end,    c985_bar1(d) + REG_MEM_WIN_BASE + i * 0x0c + 0x04);
        writel(offset, c985_bar1(d) + REG_MEM_WIN_BASE + i * 0x0c + 0x08);
    }

    writel(0x70003124, c985_bar1(d) + REG_MEM_CTL);
    writel(0x90003124, c985_bar1(d) + REG_MEM_CTL);

    dev_dbg(&d->pdev->dev, "QPHCI: reinit done\n");
    return 0;
}

/*
 * QPHCI_InitArmLoop — installs 0xEAFFFFFE (ARM branch-to-self) at card RAM
 * words 0..7 via CPR to park the ARM at the reset vector before firmware boot.
 */
int qphci_init_arm_loop(struct c985_poc *d)
{
    int i;

    dev_dbg(&d->pdev->dev, "QPHCI: init ARM loop\n");

    writel(0x00000000, c985_bar1(d) + REG_ARM_BOOT);

    for (i = 0; i < 8; i++) {
        u32 addr_field = (i << 2) & 0x1ffffffc;
        u32 ctl, tmp, status, done_code;
        unsigned long timeout;

        done_code = (d->codec.m_ChipVersion == CPR_CHIPVER_SPECIAL) ? 0x00 : 0x42;

        writel(addr_field, c985_bar1(d) + REG_CPR_WR_ADDR);

        ctl = readl(c985_bar1(d) + REG_CPR_WR_CTL);
        ctl &= 0xffff0003;
        ctl |= 0x4;
        writel(ctl, c985_bar1(d) + REG_CPR_WR_CTL);
        writel(0xeafffffe, c985_bar1(d) + REG_CPR_WR_DATA);

        timeout = jiffies + msecs_to_jiffies(CPR_TIMEOUT_MS);
        for (;;) {
            tmp = readl(c985_bar1(d) + REG_CPR_WR_CTL);
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

    dev_dbg(&d->pdev->dev, "QPHCI: init ARM loop done\n");
    return 0;
}

int QPHCI_PowerDown(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "QPHCI_PowerDown()\n");

    /* C985 (chip_type == 8) - nothing to do */
    if (d->codec.m_ChipType == 8)
        return 0;

    /* For other chip types (not C985): */
    if (d->codec.m_hci.m_access_mode == QPHCI_MODE_INDIRECT) {
        writel(0, c985_bar1(d) + 0x80c);
    }

    writel(4, c985_bar1(d) + 0xf1c);

    {
        u32 val = readl(c985_bar1(d) + 0x50);
        val |= 0x106;
        writel(val, c985_bar1(d) + 0x50);
        dev_dbg(&d->pdev->dev, "QPHCI_PowerDown() writing(0x%x) to pad control status\n", val);
    }

    writel(0, c985_bar1(d) + 0x00);
    writel(0x1ffffff, c985_bar1(d) + 0x00);

    return 0;
}

int QPHCI_Done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "QPHCI_Done()\n");

    /* Direct mode: write shutdown value to 0x840 */
    if (d->codec.m_hci.m_access_mode == QPHCI_MODE_INDIRECT)
        writel(0x10003124, c985_bar1(d) + 0x840);

    /* PowerDown does nothing for chip_type == 8 */
    QPHCI_PowerDown(d);

    dev_dbg(&d->pdev->dev, "QPHCI_Done() complete\n");

    return 0;
}

/*
 * QPHCI_GetMaxDMASize - get maximum DMA transfer size for bus type
 */
u32 QPHCI_GetMaxDMASize(struct ihciapi *hci)
{
    if (!hci)
        return 0x8000;  /* Default fallback */

        switch (hci->m_bus_type) {
            case QPHCI_BUS_USB:
                return 0x20000;

            case QPHCI_BUS_PCI:
                if (hci->m_pPCIeCntl)
                    return CPCIeCntl_GetMaxDMASize(hci->m_pPCIeCntl);
            return 0x8000;

            case QPHCI_BUS_201_EMULATION:
                return 0x8000;

            default:
                return 0xFFFFFFF;
        }
}
