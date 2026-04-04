// SPDX-License-Identifier: GPL-2.0
/*
 * project.c - Main initialization sequence for AVerMedia C985
 *
 * Boot sequence discovered through reverse engineering:
 * 1. Hardware init (NUC100, TI3101)
 * 2. Firmware upload (done in fwload.c)
 * 3. BAR0[0x04] doorbell → ARM boots, QPOS RTOS starts
 * 4. Wait for HCI thread to start
 * 5. Use BAR1 mailbox for encoder commands
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "avermedia_c985.h"
#include "nuc100.h"
#include "ti3101.h"
#include "cpr.h"
#include "qpfwencapi.h"
#include "i2c_bitbang.h"
#include "qpfwapi.h"

/* GPIO registers */
#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614

/* NUC100 GPIOs */
#define NUC100_RST_GPIO 15

/*
 * Step 1: Hardware initialization
 */
static int init_hardware(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "=== Hardware Init ===\n");

    d->mcu_addr = NUC100_I2C_ADDR >> 1;

    /* NUC100 reset (GPIO 15) */
    gpio_drive_low(d, NUC100_RST_GPIO);
    msleep(10);
    gpio_release(d, NUC100_RST_GPIO);
    msleep(100);

    ret = nuc100_check_device(d);
    if (ret) {
        dev_err(&d->pdev->dev, "NUC100 not responding\n");
        return ret;
    }

    /* TI3101 HDMI receiver (GPIO 0/2) */
    ti3101_hw_reset(d);
    ret = ti3101_probe(d);
    if (ret) {
        dev_err(&d->pdev->dev, "TI3101 probe failed\n");
        return ret;
    }

    ret = ti3101_init(d);
    if (ret) {
        dev_err(&d->pdev->dev, "TI3101 init failed\n");
        return ret;
    }

    dev_info(&d->pdev->dev, "Hardware init complete\n");
    return 0;
}

/*
 * Step 2: Check HDMI signal
 */
static int check_hdmi_signal(struct c985_poc *d)
{
    struct nuc100_hdmi_timing t;
    int valid = 0;
    int ret;

    ret = nuc100_get_hdmi_timing(d, &t, &valid);
    if (ret == 0 && valid) {
        dev_info(&d->pdev->dev,
                 "HDMI: %ux%u @ %u Hz (total %ux%u, pclk %u Hz)\n",
                 t.hactive, t.vactive,
                 (t.pixelclock * 1000) / (t.htotal * t.vtotal),
                 t.htotal, t.vtotal,
                 t.pixelclock);
        return 0;
    }

    dev_warn(&d->pdev->dev, "No valid HDMI signal detected\n");
    return -ENOLINK;
}

/*
 * Step 3: Wait for firmware boot
 * After firmware upload, ring BAR0[0x04] doorbell to start ARM
 */
static int wait_for_firmware_boot(struct c985_poc *d)
{
    u32 val;
    int i;

    dev_info(&d->pdev->dev, "=== Waiting for firmware boot ===\n");

    /* Ring doorbell */
    dev_info(&d->pdev->dev, "Ringing doorbell...\n");
    iowrite32(0x00, d->bar0 + 0x04);
    wmb();
    udelay(100);
    iowrite32(0x01, d->bar0 + 0x04);
    wmb();

    /* Wait for init messages to appear */
    msleep(1000);

    /* Check what addresses firmware gave us */
    u32 cmd_addr = ioread32(d->bar0 + 0x10);
    u32 rsp_addr = ioread32(d->bar0 + 0x14);
    dev_info(&d->pdev->dev, "cmd_addr = 0x%08x, rsp_addr = 0x%08x\n", cmd_addr, rsp_addr);

    /* Dump debug log */
    dev_info(&d->pdev->dev, "ARM debug log:\n");
    for (i = 0; i < 40; i++) {
        cpr_read(d, 0x563d8 + (i * 4), &val);
        if (val == 0) continue;

        char c[5] = {val & 0xff, (val >> 8) & 0xff,
            (val >> 16) & 0xff, (val >> 24) & 0xff, 0};
            dev_info(&d->pdev->dev, "  [%2d] '%c%c%c%c'\n", i,
                     (c[0] >= 0x20 && c[0] < 0x7f) ? c[0] : '.',
                     (c[1] >= 0x20 && c[1] < 0x7f) ? c[1] : '.',
                     (c[2] >= 0x20 && c[2] < 0x7f) ? c[2] : '.',
                     (c[3] >= 0x20 && c[3] < 0x7f) ? c[3] : '.');
    }

    /* Now send SystemOpen command via ARM memory */
    dev_info(&d->pdev->dev, "Sending SystemOpen via cpr_write to 0x%08x\n", cmd_addr);

    cpr_write(d, cmd_addr + 0x00, 0x000000F1);  /* command = SystemOpen */
    cpr_write(d, cmd_addr + 0x04, 0x80000011);  /* function */
    cpr_write(d, cmd_addr + 0x08, 0x00000000);
    cpr_write(d, cmd_addr + 0x0c, 0x00000000);

    /* Ring doorbell again */
    iowrite32(0x00, d->bar0 + 0x04);
    wmb();
    udelay(100);
    iowrite32(0x01, d->bar0 + 0x04);
    wmb();

    msleep(500);

    /* Check for new debug messages */
    dev_info(&d->pdev->dev, "Debug log after command:\n");
    for (i = 32; i < 64; i++) {
        cpr_read(d, 0x563d8 + (i * 4), &val);
        if (val == 0) continue;

        char c[5] = {val & 0xff, (val >> 8) & 0xff,
            (val >> 16) & 0xff, (val >> 24) & 0xff, 0};
            dev_info(&d->pdev->dev, "  [%2d] '%c%c%c%c'\n", i,
                     (c[0] >= 0x20 && c[0] < 0x7f) ? c[0] : '.',
                     (c[1] >= 0x20 && c[1] < 0x7f) ? c[1] : '.',
                     (c[2] >= 0x20 && c[2] < 0x7f) ? c[2] : '.',
                     (c[3] >= 0x20 && c[3] < 0x7f) ? c[3] : '.');
    }

    /* Check response buffer */
    dev_info(&d->pdev->dev, "Response buffer:\n");
    for (i = 0; i < 8; i++) {
        cpr_read(d, rsp_addr + (i * 4), &val);
        dev_info(&d->pdev->dev, "  rsp[%d] = 0x%08x\n", i, val);
    }

    /* Check ready_queue */
    cpr_read(d, 0x5eca0, &val);
    dev_info(&d->pdev->dev, "ready_queue = 0x%08x\n", val);

    return 0;  /* Don't fail - let's see what happens */
}

/*
 * Step 4: Send SystemOpen command via BAR1 mailbox
 */
static int encoder_system_open(struct c985_poc *d)
{
    u32 val;

    dev_info(&d->pdev->dev, "=== Encoder SystemOpen via ARM memory ===\n");

    /* Try writing command directly to ARM memory instead of BAR1 */
    /* Use the area we know the ARM can see */

    /* First, find where the HCI thread expects commands */
    /* From BAR0[0x10], we saw cmd buffer addresses like 0x19xxxx */
    u32 cmd_addr = ioread32(d->bar0 + 0x10);
    dev_info(&d->pdev->dev, "BAR0[0x10] cmd_addr = 0x%08x\n", cmd_addr);

    /* Write command directly to ARM memory */
    cpr_write(d, cmd_addr + 0x00, 0x000000F1);  /* SystemOpen command */
    cpr_write(d, cmd_addr + 0x04, 0x80000011);  /* function parameter */
    cpr_write(d, cmd_addr + 0x08, 0x00000000);
    cpr_write(d, cmd_addr + 0x0c, 0x00000000);

    /* Ring doorbell again to signal new command? */
    iowrite32(0x00, d->bar0 + 0x04);
    wmb();
    udelay(100);
    iowrite32(0x01, d->bar0 + 0x04);
    wmb();

    msleep(200);

    /* Check response */
    u32 rsp_addr = ioread32(d->bar0 + 0x14);
    dev_info(&d->pdev->dev, "BAR0[0x14] rsp_addr = 0x%08x\n", rsp_addr);

    for (int i = 0; i < 8; i++) {
        cpr_read(d, rsp_addr + (i * 4), &val);
        if (val != 0)
            dev_info(&d->pdev->dev, "  rsp[%d] = 0x%08x\n", i, val);
    }

    /* Check if ready_queue changed */
    cpr_read(d, 0x5eca0, &val);
    dev_info(&d->pdev->dev, "ready_queue after cmd = 0x%08x\n", val);

    return 0;
}
/*
 * Main initialization entry point
 */
int project_c985_init(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "=== AVerMedia C985 Initialization ===\n");

    /* Step 1: Hardware init */
    ret = init_hardware(d);
    if (ret)
        return ret;

    /* Step 2: Check HDMI (non-fatal) */
    check_hdmi_signal(d);

    /* Step 3: Ring doorbell to wake ARM */
    dev_info(&d->pdev->dev, "Waking ARM processor\n");
    arm_ring_doorbell(d);

    /* Wait for firmware boot */
    msleep(1000);

    /* Step 4: Initialize command interface */
    ret = QPFWAPI_Init(d);
    if (ret)
        return ret;

    /* Step 5: SystemOpen with task_id=8, function=0x80000011 */
    ret = QPFWCODECAPI_SystemOpen(d, 8, 0x80000011);
    if (ret)
        return ret;

    dev_info(&d->pdev->dev, "C985 initialization complete\n");
    return 0;
}


