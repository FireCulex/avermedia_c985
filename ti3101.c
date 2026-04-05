// SPDX-License-Identifier: GPL-2.0
// ti3101.c — TI3101 audio codec driver using bit-bang I2C

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"
#include "ti3101.h"

#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614

static int ti3101_write(struct c985_poc *d, u8 reg, u8 val)
{
    u8 addr7 = TI3101_CHIP_ADDR >> 1;

    i2c_start(d, I2C_SCL_TI3101, I2C_SDA_TI3101);

    if (!i2c_write(d, I2C_SCL_TI3101, I2C_SDA_TI3101, (addr7 << 1) | 0)) {
        i2c_stop(d, I2C_SCL_TI3101, I2C_SDA_TI3101);
        return -EIO;
    }

    if (!i2c_write(d, I2C_SCL_TI3101, I2C_SDA_TI3101, reg)) {
        i2c_stop(d, I2C_SCL_TI3101, I2C_SDA_TI3101);
        return -EIO;
    }

    if (!i2c_write(d, I2C_SCL_TI3101, I2C_SDA_TI3101, val)) {
        i2c_stop(d, I2C_SCL_TI3101, I2C_SDA_TI3101);
        return -EIO;
    }

    i2c_stop(d, I2C_SCL_TI3101, I2C_SDA_TI3101);
    return 0;
}

int ti3101_read_reg(struct c985_poc *d, u8 reg, u8 *out)
{
    u8 addr7 = TI3101_CHIP_ADDR >> 1;
    return i2c_write_then_read(d, I2C_SCL_TI3101, I2C_SDA_TI3101, addr7, reg, out, 1);
}

void ti3101_hw_reset(struct c985_poc *d)
{
    u32 dir, val;

    dev_vdbg(&d->pdev->dev, "TI3101: HwReset\n");

    dir = readl(d->bar1 + REG_GPIO_DIR);
    dir |= BIT(GPIO_TI3101_RST);
    writel(dir, d->bar1 + REG_GPIO_DIR);
    msleep(10);

    val = readl(d->bar1 + REG_GPIO_VAL);
    val &= ~BIT(GPIO_TI3101_RST);
    writel(val, d->bar1 + REG_GPIO_VAL);
    msleep(10);

    val |= BIT(GPIO_TI3101_RST);
    writel(val, d->bar1 + REG_GPIO_VAL);

    dev_vdbg(&d->pdev->dev, "TI3101: HwReset done\n");
}

int ti3101_probe(struct c985_poc *d)
{
    u8 addr7 = TI3101_CHIP_ADDR >> 1;
    int ack;

    i2c_start(d, I2C_SCL_TI3101, I2C_SDA_TI3101);
    ack = i2c_write(d, I2C_SCL_TI3101, I2C_SDA_TI3101, (addr7 << 1) | 0);
    i2c_stop(d, I2C_SCL_TI3101, I2C_SDA_TI3101);

    if (!ack) {
        dev_err(&d->pdev->dev, "TI3101: probe NAK\n");
        return -EIO;
    }

    dev_vdbg(&d->pdev->dev, "TI3101: probe ACK\n");
    return 0;
}

int ti3101_set_volume(struct c985_poc *d, u32 vol)
{
    u8 v, tmp;
    int ret;

    dev_vdbg(&d->pdev->dev, "TI3101: setVolume(%u)\n", vol);

    if (vol == 6) {
        ret = ti3101_write(d, 0x0F, 0x0C); if (ret) return ret;
        ret = ti3101_write(d, 0x10, 0x0C); if (ret) return ret;
        ret = ti3101_write(d, 0x13, 0x04); if (ret) return ret;
        ret = ti3101_write(d, 0x16, 0x04); if (ret) return ret;
    }
    else if (vol == 0) {
        ret = ti3101_write(d, 0x0F, 0x80); if (ret) return ret;
        ret = ti3101_write(d, 0x10, 0x80); if (ret) return ret;

        ret = ti3101_read_reg(d, 0x13, &v);
        if (!ret) {
            ret = ti3101_write(d, 0x13, v & 0x83);
            if (ret) return ret;
        }

        ret = ti3101_read_reg(d, 0x16, &v);
        if (!ret) {
            ret = ti3101_write(d, 0x16, v & 0x83);
            if (ret) return ret;
        }
    }
    else if (vol < 6) {
        tmp = (6 - vol) * 8 + 4;
        ret = ti3101_write(d, 0x0F, 0x0C); if (ret) return ret;
        ret = ti3101_write(d, 0x10, 0x0C); if (ret) return ret;
        ret = ti3101_write(d, 0x13, tmp);  if (ret) return ret;
        ret = ti3101_write(d, 0x16, tmp);  if (ret) return ret;
    }
    else if (vol >= 7) {
        tmp = (u8)vol * 4 + 0xE8;
        if (vol > 11)
            tmp = 0x14;
        tmp += 0x0C;

        ret = ti3101_write(d, 0x0F, tmp); if (ret) return ret;
        ret = ti3101_write(d, 0x10, tmp); if (ret) return ret;
        ret = ti3101_write(d, 0x13, 0x04); if (ret) return ret;
        ret = ti3101_write(d, 0x16, 0x04); if (ret) return ret;
    }
    else {
        dev_err(&d->pdev->dev, "TI3101: illegal volume %u\n", vol);
        return -EINVAL;
    }

    return 0;
}

int ti3101_init(struct c985_poc *d)
{
    int ret;

    ret = ti3101_write(d, 0x03, 0x91); if (ret) return ret;
    ret = ti3101_write(d, 0x04, 0x20); if (ret) return ret;
    ret = ti3101_write(d, 0x06, 0x00); if (ret) return ret;
    ret = ti3101_write(d, 0x08, 0xC0); if (ret) return ret;
    ret = ti3101_write(d, 0x09, 0x20); if (ret) return ret;
    ret = ti3101_write(d, 0x0C, 0x50); if (ret) return ret;

    ret = ti3101_set_volume(d, TI3101_DEFAULT_VOLUME);
    if (ret) return ret;

    ret = ti3101_write(d, 0x15, 0x78); if (ret) return ret;
    ret = ti3101_write(d, 0x18, 0x78); if (ret) return ret;
    ret = ti3101_write(d, 0x0F, 0x04); if (ret) return ret;
    ret = ti3101_write(d, 0x10, 0x04); if (ret) return ret;

    dev_info(&d->pdev->dev, "TI3101: init done\n");
    return 0;
}
