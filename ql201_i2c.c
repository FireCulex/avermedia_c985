// SPDX-License-Identifier: GPL-2.0
// ql201_i2c.c — QL201 hardware I2C engine for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "ql201_i2c.h"

#define REG_I2C_CTRL    0x0500
#define REG_I2C_W0      0x0504
#define REG_I2C_W1      0x0508
#define REG_I2C_R0      0x050c
#define REG_I2C_R1      0x0510

#define I2C_TIMEOUT_US  500000

int ql201_i2c_write(struct c985_poc *d, u8 addr, const u8 *buf, int len)
{
    u32 w0 = 0, w1 = 0, ctrl;
    int i;
    unsigned long timeout;

    if (!buf || len < 0 || len > 8)
        return -EINVAL;

    for (i = 0; i < len && i < 4; i++)
        ((u8 *)&w0)[i] = buf[i];
    for (; i < len; i++)
        ((u8 *)&w1)[i - 4] = buf[i];

    writel(w0, d->bar1 + REG_I2C_W0);
    if (len > 4)
        writel(w1, d->bar1 + REG_I2C_W1);

    ctrl = (addr << 7) | (len & 7) | 0x80000000;
    writel(ctrl, d->bar1 + REG_I2C_CTRL);

    timeout = I2C_TIMEOUT_US;
    while (timeout--) {
        ctrl = readl(d->bar1 + REG_I2C_CTRL);
        if (!(ctrl & 0x80000000))
            return 0;
        udelay(1);
    }

    dev_err(&d->pdev->dev, "QL201 I2C: timeout addr=0x%02x ctrl=0x%08x\n", addr, ctrl);
    return -ETIMEDOUT;
}

int ql201_i2c_write_read(struct c985_poc *d, u8 addr,
                         const u8 *wbuf, int wlen,
                         u8 *rbuf, int rlen)
{
    u32 w0 = 0, w1 = 0, r0, r1, ctrl;
    int i;
    unsigned long timeout;

    if (wlen < 0 || wlen > 8 || rlen < 0 || rlen > 8)
        return -EINVAL;

    for (i = 0; i < wlen && i < 4; i++)
        ((u8 *)&w0)[i] = wbuf[i];
    for (; i < wlen; i++)
        ((u8 *)&w1)[i - 4] = wbuf[i];

    if (wlen > 0) {
        writel(w0, d->bar1 + REG_I2C_W0);
        if (wlen > 4)
            writel(w1, d->bar1 + REG_I2C_W1);
    }

    ctrl = (addr << 7) | (wlen & 7) | ((rlen & 7) << 3) | 0x80000000;
    writel(ctrl, d->bar1 + REG_I2C_CTRL);

    timeout = I2C_TIMEOUT_US;
    while (timeout--) {
        ctrl = readl(d->bar1 + REG_I2C_CTRL);
        if (!(ctrl & 0x80000000))
            return 0; // Success, busy bit cleared
        udelay(1); // Wait a microsecond before checking again
    }

    dev_err(&d->pdev->dev, "QL201 I2C: timeout addr=0x%02x ctrl=0x%08x\n", addr, ctrl);
    return -ETIMEDOUT;

    if (rlen > 0) {
        r0 = readl(d->bar1 + REG_I2C_R0);
        if (rlen > 4)
            r1 = readl(d->bar1 + REG_I2C_R1);

        for (i = 0; i < rlen && i < 4; i++)
            rbuf[i] = ((u8 *)&r0)[i];
        for (; i < rlen; i++)
            rbuf[i] = ((u8 *)&r1)[i - 4];
    }

    return 0;
}

int ql201_i2c_debug_ping(struct c985_poc *d, u8 addr)
{
    u8 reg = 0x00, val = 0;
    int ret;

    dev_info(&d->pdev->dev, "QL201 I2C: ping addr=0x%02x\n", addr);

    ret = ql201_i2c_write_read(d, addr, &reg, 1, &val, 1);
    if (ret) {
        dev_err(&d->pdev->dev, "QL201 I2C: ping failed ret=%d\n", ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "QL201 I2C: ping OK val=0x%02x\n", val);
    return 0;
}
