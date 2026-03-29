// SPDX-License-Identifier: GPL-2.0
// nuc100.c — NUC100 MCU interface via bit-bang I2C

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"
#include "nuc100.h"

static int nuc100_read_reg(struct c985_poc *d, u8 reg, u8 *val)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 0)) goto fail;
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, reg)) goto fail;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 1)) goto fail;

    *val = i2c_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, 0);
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return 0;

    fail:
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return -EIO;
}

static int nuc100_read_block(struct c985_poc *d, u8 reg, u8 *buf, int len)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    int i;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 0)) goto fail;
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, reg)) goto fail;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 1)) goto fail;

    for (i = 0; i < len; i++)
        buf[i] = i2c_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (i < len - 1) ? 1 : 0);

    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return 0;

    fail:
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
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

int nuc100_check_device(struct c985_poc *d)
{
    u8 id[3], ver;
    int ret;
    u8 addr7 = NUC100_I2C_ADDR >> 1;

    msleep(5);

    ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, addr7, 0x0b, id, 3);
    if (ret)
        return ret;

    if (id[0] == 0x39 && id[1] == 0x38 && id[2] == 0x35) {
        dev_info(&d->pdev->dev, "NUC100: FW ID OK (0x%02x%02x%02x)\n",
                 id[0], id[1], id[2]);

        ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, addr7, 0x04, &ver, 1);
        if (ret == 0)
            dev_info(&d->pdev->dev, "NUC100: FW version 0x%02x\n", ver);

        return 0;
    }

    dev_err(&d->pdev->dev, "NUC100: bad FW ID 0x%02x%02x%02x (want 0x393835)\n",
            id[0], id[1], id[2]);
    return -ENODEV;
}

int nuc100_init(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "NUC100: init\n");

    d->mcu_addr = NUC100_I2C_ADDR >> 1;

    ret = nuc100_check_device(d);
    if (ret)
        dev_warn(&d->pdev->dev, "NUC100 check failed (might be in ISP mode)\n");

    return 0;
}
