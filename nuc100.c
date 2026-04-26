// SPDX-License-Identifier: GPL-2.0
// nuc100.c — NUC100 MCU interface via bit-bang I2C

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "structs.h"
#include "avermedia_c985.h"
#include "i2c_bitbang.h"
#include "nuc100.h"

/* ============================================
 * Low-level I2C helpers
 * ============================================ */

int nuc100_read_reg(struct c985_poc *d, u8 reg, u8 *val)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 0))
        goto fail;
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, reg))
        goto fail;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 1))
        goto fail;

    *val = i2c_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100, 0);
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return 0;

    fail:
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return -EIO;
}

/*
 * Write a block of bytes to NUC100.
 * buf[0] is the register address, buf[1..len-1] is data.
 */
static int nuc100_write_block(struct c985_poc *d, const u8 *buf, int len)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    int i;

    i2c_start(d, I2C_SCL_NUC100, I2C_SDA_NUC100);

    if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, (addr7 << 1) | 0))
        goto fail;

    for (i = 0; i < len; i++) {
        if (!i2c_write(d, I2C_SCL_NUC100, I2C_SDA_NUC100, buf[i]))
            goto fail;
    }

    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return 0;

    fail:
    i2c_stop(d, I2C_SCL_NUC100, I2C_SDA_NUC100);
    return -EIO;
}

/* ============================================
 * NUC100 busy wait helper
 * ============================================ */

static int nuc100_wait_ready(struct c985_poc *d, int timeout_ms)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 busy;
    int retries = 0;
    int max_retries = timeout_ms / 30;

    if (max_retries < 1)
        max_retries = 1;

    do {
        if (i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
            addr7, 0x1b, &busy, 1) < 0)
            return -EIO;
        if (busy == 0)
            return 0;
        msleep(30);
    } while (++retries < max_retries);

    return -ETIMEDOUT;
}

/* ============================================
 * HDMI Status Detection
 * ============================================ */

int nuc100_get_hdmi_status(struct c985_poc *d)
{
    static unsigned long last_check;
    unsigned long now = jiffies;
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 status;
    int ret;

    /* Rate limit: minimum 50ms between checks */
    if (last_check && time_before(now, last_check + msecs_to_jiffies(50)))
        return d->hdmi_valid;

    last_check = now;

    msleep(5);

    ret = nuc100_wait_ready(d, 300);
    if (ret < 0)
        return ret;

    msleep(5);

    if (i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
        addr7, 0x1c, &status, 1) < 0)
        return -EIO;

    if (status & 0x04) {
        if (!d->hdmi_valid) {
            /* Signal just appeared - invalidate cache */
            d->hdmi_info_cached = 0;
            dev_info(&d->pdev->dev, "HDMI: signal detected (status=0x%02x)\n", status);
        }
        d->hdmi_valid = 1;
        return 1;
    }

    if (d->hdmi_valid) {
        /* Signal just lost - invalidate cache */
        d->hdmi_info_cached = 0;
        dev_info(&d->pdev->dev, "HDMI: no signal (status=0x%02x)\n", status);
    }
    d->hdmi_valid = 0;
    return 0;
}

/* ============================================
 * HDMI Timing Info
 * ============================================ */

/**
 * nuc100_getHdmiVideo_6604 - Get HDMI timing info from NUC100
 * @d: device context
 * @info: output HDMI info structure (may be NULL to just check validity)
 * @valid: output validity flag (1 = valid signal, 0 = no signal)
 *
 * Reads HDMI timing from NUC100 MCU via I2C.
 * Based on InterfaceNUC100::getHdmiVideo_6604() from Windows driver.
 *
 * Returns 0 on success, negative error code on failure.
 */
int nuc100_getHdmiVideo_6604(struct c985_poc *d,
                             struct hdmi_info *info,
                             int *valid)
{
    static unsigned long last_timing_check;
    unsigned long now = jiffies;
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 buf[7];
    u8 pol;
    int ret;
    u16 htotal, hactive, vtotal, vactive;
    s32 pclk;
    u8 hpol, vpol;

    /* Check cache first - use cached data if fresh */
    if (last_timing_check &&
        time_before(now, last_timing_check + msecs_to_jiffies(100)) &&
        d->hdmi_info_cached) {
        if (info)
            memcpy(info, &d->cached_hdmi_info, sizeof(*info));
        if (valid)
            *valid = d->hdmi_valid;
        return 0;
        }

        if (valid)
            *valid = 0;

    /* Step 1: Wait for NUC100 timing ready (reg 0x1B == 0) */
    msleep(5);

    ret = nuc100_wait_ready(d, 300);
    if (ret < 0) {
        dev_dbg(&d->pdev->dev, "NUC100 timing not ready\n");
        return ret;
    }

    /* Step 2: Read 7 timing bytes from reg 0x1D */
    msleep(5);

    ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
                              addr7, 0x1d, buf, 7);
    if (ret < 0)
        return ret;

    /* Decode timing (matches Windows bit-packing) */
    htotal  = ((buf[1] & 0x0F) << 8) | buf[0];
    hactive = ((buf[1] & 0xF0) << 4) | buf[2];
    vtotal  = ((buf[4] & 0x0F) << 8) | buf[3];
    vactive = ((buf[4] & 0xF0) << 4) | buf[5];

    /* Pixel clock: 0x34BC00 / PixelCNT (in kHz) */
    pclk = 0;
    if (buf[6] != 0) {
        pclk = 0x34BC00 / (int)buf[6];
    } else {
        dev_dbg(&d->pdev->dev, "Can't get PixelCNT from NUC100\n");
    }

    /* Basic sanity check */
    if (htotal == 0 || hactive == 0 || vtotal == 0 || vactive == 0) {
        dev_dbg(&d->pdev->dev,
                "Invalid timing: %ux%u total %ux%u\n",
                hactive, vactive, htotal, vtotal);
        return 0;
    }

    /* Step 3: Read sync polarity from reg 0x26 */
    ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
                              addr7, 0x26, &pol, 1);
    if (ret < 0)
        return ret;

    /* Decode polarity (matches Windows logic) */
    switch (pol | 0xFC) {
        case 0xFD:
            hpol = 0;
            vpol = 1;
            break;
        case 0xFE:
            hpol = 1;
            vpol = 0;
            break;
        case 0xFF:
            hpol = 1;
            vpol = 1;
            break;
        default:
            hpol = 0;
            vpol = 0;
            break;
    }

    /* Update cache timestamp */
    last_timing_check = now;

    /* Fill output struct */
    if (info) {
        memset(info, 0, sizeof(*info));
        info->HActive = hactive;
        info->VActive = vactive;
        info->HTotal = htotal;
        info->VTotal = vtotal;
        info->PCLK = pclk;
        info->xCnt = buf[6];
        info->ScanMode = 0;  /* TODO: detect interlaced */
        info->VPolarity = vpol;
        info->HPolarity = hpol;
        info->Rate = 0;      /* TODO: calculate from pclk/htotal/vtotal */
        info->QP_InCtrl = 0;
        info->QP_InRes = 0;
        info->QP_InSync = 0;
    }

    /* Cache the result */
    memcpy(&d->cached_hdmi_info, info ? info : &d->cached_hdmi_info, sizeof(d->cached_hdmi_info));
    if (info) {
        d->cached_hdmi_info = *info;
    } else {
        /* Build cache even if caller didn't want info */
        d->cached_hdmi_info.HActive = hactive;
        d->cached_hdmi_info.VActive = vactive;
        d->cached_hdmi_info.HTotal = htotal;
        d->cached_hdmi_info.VTotal = vtotal;
        d->cached_hdmi_info.PCLK = pclk;
        d->cached_hdmi_info.xCnt = buf[6];
        d->cached_hdmi_info.VPolarity = vpol;
        d->cached_hdmi_info.HPolarity = hpol;
    }
    d->hdmi_info_cached = 1;
    d->hdmi_valid = 1;

    if (valid)
        *valid = 1;

    dev_dbg(&d->pdev->dev,
            "HDMI: %ux%u @ %d kHz (total %ux%u, pol H%u V%u)\n",
            hactive, vactive, pclk, htotal, vtotal, hpol, vpol);

    return 0;
}

/* ============================================
 * Device Check / Initialization
 * ============================================ */

int nuc100_check_device(struct c985_poc *d)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 id[3];
    u8 ver = 0;
    int ret;

    msleep(5);

    ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
                              addr7, 0x0b, id, 3);
    if (ret)
        return ret;

    if (id[0] != 0x39 || id[1] != 0x38 || id[2] != 0x35) {
        dev_err(&d->pdev->dev,
                "NUC100: bad ID 0x%02x%02x%02x (expected 0x393835)\n",
                id[0], id[1], id[2]);
        return -ENODEV;
    }

    ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
                              addr7, 0x04, &ver, 1);
    if (ret)
        dev_warn(&d->pdev->dev, "NUC100: ID 0x393835 (version read failed)\n");
    else
        dev_info(&d->pdev->dev, "NUC100: ID 0x393835, FW v0x%02x\n", ver);

    return 0;
}

int nuc100_init(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "NUC100: init\n");

    d->m_McuAddr = NUC100_I2C_ADDR >> 1;
    d->hdmi_valid = 0;
    d->hdmi_info_cached = 0;

    ret = nuc100_check_device(d);
    if (ret)
        dev_warn(&d->pdev->dev, "NUC100 check failed (might be in ISP mode)\n");

    return 0;
}

/* ============================================
 * Register Access via NUC100 (downstream chips)
 * ============================================ */

int nuc100_access_regs(struct c985_poc *d, struct nuc100_params *p)
{
    u8 addr7 = NUC100_I2C_ADDR >> 1;
    u8 buf[6];
    int ret;

    msleep(10);

    if (p->command == 0x01) {
        /* WRITE: program NUC100 to write 1 byte to downstream chip */
        buf[0] = 0x0e;
        buf[1] = p->chip;
        buf[2] = p->command;
        buf[3] = p->reg_address;
        buf[4] = 0x01;
        ret = nuc100_write_block(d, buf, 5);
        if (ret < 0)
            return ret;

        msleep(5);

        buf[0] = 0x13;
        buf[1] = p->data[0];
        ret = nuc100_write_block(d, buf, 2);
        if (ret < 0)
            return ret;

        msleep(5);

        buf[0] = 0x12;
        buf[1] = 0x01;
        ret = nuc100_write_block(d, buf, 2);
        if (ret < 0)
            return ret;

    } else if (p->command == 0x00) {
        /* READ: program NUC100 to read 1 byte from downstream chip */
        buf[0] = 0x0e;
        buf[1] = p->chip;
        buf[2] = p->command;
        buf[3] = p->reg_address;
        buf[4] = 0x01;
        buf[5] = 0x01;
        ret = nuc100_write_block(d, buf, 6);
        if (ret < 0)
            return ret;

        msleep(15);

        ret = i2c_write_then_read(d, I2C_SCL_NUC100, I2C_SDA_NUC100,
                                  addr7, 0x13, &p->data[0], 1);
        if (ret < 0)
            return ret;
    } else {
        dev_warn(&d->pdev->dev,
                 "NUC100: invalid command %u in access_regs\n",
                 p->command);
        return -EINVAL;
    }

    return 0;
}
