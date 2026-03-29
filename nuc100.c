// SPDX-License-Identifier: GPL-2.0
// nuc100.c — NUC100 MCU interface via bit-bang I2C

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"
#include "nuc100.h"


int nuc100_read_reg(struct c985_poc *d, u8 reg, u8 *val)
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

int nuc100_get_hdmi_timing(struct c985_poc *d,
                           struct nuc100_hdmi_timing *t,
                           int *valid)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 buf[7];
    u8 busy, pol;
    int retries = 0;
    int ret;

    if (valid)
        *valid = 0;

    /* Step 1: wait for NUC100 timing ready (reg 0x1B == 0) */
    msleep(5);

    do {
        ret = i2c_write_then_read(d,
                                  I2C_SCL_NUC100,
                                  I2C_SDA_NUC100,
                                  addr7,
                                  0x1b,
                                  &busy,
                                  1);
        if (ret < 0)
            return ret;

        if (busy == 0)
            break;

        msleep(30);
    } while (++retries < 10);

    if (busy != 0)
        return -EIO;

    /* Step 2: read 7 timing bytes from reg 0x1D */
    msleep(5);

    ret = i2c_write_then_read(d,
                              I2C_SCL_NUC100,
                              I2C_SDA_NUC100,
                              addr7,
                              0x1d,
                              buf,
                              7);
    if (ret < 0)
        return ret;

    /* Decode timing (matches Windows bit‑packing) */
    u16 htotal  = (buf[1] & 0x0F) << 8 | buf[0];
    u16 hactive = (buf[1] & 0xF0) << 4 | buf[2];
    u16 vtotal  = (buf[4] & 0x0F) << 8 | buf[3];
    u16 vactive = (buf[4] & 0xF0) << 4 | buf[5];

    /* Pixel clock: 0x34BC00 / PixelCNT */
    u8 pixelcnt = buf[6];
    u32 pclk = 0;

    if (pixelcnt != 0)
        pclk = 0x34BC00 / pixelcnt;

    /* Basic sanity check */
    if (htotal == 0 || hactive == 0 ||
        vtotal == 0 || vactive == 0) {
        if (valid)
            *valid = 0;
        return 0;
        }

        /* Step 3: read sync polarity from reg 0x26 */
        ret = i2c_write_then_read(d,
                                  I2C_SCL_NUC100,
                                  I2C_SDA_NUC100,
                                  addr7,
                                  0x26,
                                  &pol,
                                  1);
        if (ret < 0)
            return ret;

    u8 hpol = 0;
    u8 vpol = 0;

    switch (pol | 0xFC) {
        case 0xFD: hpol = 0; vpol = 1; break;
        case 0xFE: hpol = 1; vpol = 0; break;
        case 0xFF: hpol = 1; vpol = 1; break;
        default:   hpol = 0; vpol = 0; break;
    }

    /* Fill output struct */
    if (t) {
        t->hactive    = hactive;
        t->vactive    = vactive;
        t->htotal     = htotal;
        t->vtotal     = vtotal;
        t->pixelclock = pclk;
        t->hpol       = hpol;
        t->vpol       = vpol;
    }

    if (valid)
        *valid = 1;

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

int nuc100_get_hdmi_status(struct c985_poc *d)
{
    u8 busy, status;
    int retries = 0;
    u8 addr7 = NUC100_I2C_ADDR >> 1;

    msleep(5);

    /* Wait for NUC100 to be ready (register 0x1B == 0) */
    do {
        if (i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, addr7, 0x1b, &busy, 1) < 0)
            return -EIO;
        if (busy == 0)
            break;
        msleep(30);
    } while (++retries < 10);

    if (busy != 0)
        return -EIO;

    msleep(5);

    /* Read status register 0x1C */
    if (i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, addr7, 0x1c, &status, 1) < 0)
        return -EIO;

    /* Bit 2 = signal present */
    if (status & 0x04) {
        dev_info(&d->pdev->dev, "HDMI: signal detected (status=0x%02x)\n", status);
        return 1;
    }

    dev_info(&d->pdev->dev, "HDMI: no signal (status=0x%02x)\n", status);
    return 0;
}

