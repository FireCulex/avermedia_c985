// SPDX-License-Identifier: GPL-2.0
// project.c — ProjectC985 initialization

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "avermedia_c985.h"
#include "nuc100.h"
#include "ti3101.h"
#include "i2c_bitbang.h"

/* GPIO registers */
#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614

int project_c985_init(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "project_c985_init()\n");

    d->mcu_addr = NUC100_I2C_ADDR >> 1;

    /* NUC100 reset and check (GPIO 14/15) */
    gpio_drive_low(d, NUC100_RST_GPIO);
    msleep(10);
    gpio_release(d, NUC100_RST_GPIO);
    msleep(100);

    ret = nuc100_check_device(d);
    if (ret)
        dev_warn(&d->pdev->dev, "NUC100 check failed\n");

    /* TI3101 (GPIO 0/2) */
    ti3101_hw_reset(d);
    ret = ti3101_probe(d);
    if (ret) {
        dev_warn(&d->pdev->dev, "TI3101 probe failed\n");
        return ret;
    }

    ret = ti3101_init(d);
    if (ret) {
        dev_warn(&d->pdev->dev, "TI3101 init failed\n");
        return ret;
    }

    struct nuc100_hdmi_timing t;
    int valid = 0;

    ret = nuc100_get_hdmi_timing(d, &t, &valid);
    if (ret == 0 && valid) {
        dev_info(&d->pdev->dev,
                 "HDMI timing: %ux%u total=%ux%u pclk=%u Hz HPol=%u VPol=%u\n",
                 t.hactive, t.vactive,
                 t.htotal, t.vtotal,
                 t.pixelclock,
                 t.hpol, t.vpol);
    } else {
        dev_warn(&d->pdev->dev, "HDMI timing invalid or unreadable\n");
    }

    return 0;
}
