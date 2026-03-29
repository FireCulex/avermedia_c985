// SPDX-License-Identifier: GPL-2.0
// nuc100.c — NUC100 HDMI timing readout via bit-bang I2C

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"
#include "nuc100.h"

#define NUC100_ADDR  0x15

static int nuc100_read_reg(struct c985_poc *d, u8 reg, u8 *val)
{
    u8 addr = NUC100_ADDR << 1;

    i2c_bb_start(d);
    if (!i2c_bb_write_byte(d, addr | 0)) goto fail;
    if (!i2c_bb_write_byte(d, reg))      goto fail;

    i2c_bb_start(d);
    if (!i2c_bb_write_byte(d, addr | 1)) goto fail;

    *val = i2c_bb_read_byte(d, 0);
    i2c_bb_stop(d);
    return 0;

    fail:
    i2c_bb_stop(d);
    return -EIO;
}

static int nuc100_read_block(struct c985_poc *d, u8 reg, u8 *buf, int len)
{
    u8 addr = NUC100_ADDR << 1;
    int i;

    i2c_bb_start(d);
    if (!i2c_bb_write_byte(d, addr | 0)) goto fail;
    if (!i2c_bb_write_byte(d, reg))      goto fail;

    i2c_bb_start(d);
    if (!i2c_bb_write_byte(d, addr | 1)) goto fail;

    for (i = 0; i < len; i++)
        buf[i] = i2c_bb_read_byte(d, (i < len - 1) ? 1 : 0);

    i2c_bb_stop(d);
    return 0;

    fail:
    i2c_bb_stop(d);
    return -EIO;
}

int nuc100_get_hdmi_timing(struct c985_poc *d)
{
    u8 buf[7], busy;
    int retries = 0;
    u16 HTotal, HActive, VTotal, VActive;
    u32 PCLK;

    do {
        if (nuc100_read_reg(d, 0x1b, &busy) < 0)
            return -EIO;
        if (busy == 0)
            break;
        msleep(30);
    } while (++retries < 10);

    if (busy != 0)
        return -EIO;

    if (nuc100_read_block(d, 0x1d, buf, 7) < 0)
        return -EIO;

    HTotal  =  buf[0] | ((buf[1] & 0x0f) << 8);
    HActive = ((buf[1] & 0xf0) << 4) | buf[2];
    VTotal  =  buf[3] | ((buf[4] & 0x0f) << 8);
    VActive = ((buf[4] & 0xf0) << 4) | buf[5];
    PCLK    = buf[6] ? (0x34bc00u / buf[6]) : 0;

    dev_info(&d->pdev->dev,
             "NUC100: %u x %u, total %u x %u, PCLK ~ %u\n",
             HActive, VActive, HTotal, VTotal, PCLK);

    return 0;
}
