// SPDX-License-Identifier: GPL-2.0
// cqlcodec.c — CQLCodec device initialization for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "avermedia_c985.h"
#include "cqlcodec.h"
#include "cpr.h"
#include "diag.h"
#include "ql201_i2c.h"
#include "ti3101.h"
#include "nuc100.h"
#include "qphci.h"

#define DRV_NAME "avermedia_c985_poc"

/* -----------------------------------------------------------------------
 * Register helpers
 * --------------------------------------------------------------------- */

static inline void c985_wr(struct c985_poc *d, u32 off, u32 val)
{
    dev_dbg(&d->pdev->dev, "WR [0x%04x] = 0x%08x\n", off, val);
    writel(val, d->bar1 + off);
}

/* -----------------------------------------------------------------------
 * Debug dumps
 * --------------------------------------------------------------------- */

static void c985_dump_regs(struct c985_poc *d, const char *tag)
{
    dev_info(&d->pdev->dev, "--- regs [%s] ---\n", tag);
    dev_info(&d->pdev->dev, "  ARM_CTRL=0x%08x CHIP_VER=0x%08x\n",
             readl(d->bar1 + REG_ARM_CTRL),
             readl(d->bar1 + REG_CHIP_VER));
    dev_info(&d->pdev->dev, "  ARM_RESET=0x%08x ARM_BOOT=0x%08x\n",
             readl(d->bar1 + REG_ARM_RESET),
             readl(d->bar1 + REG_ARM_BOOT));
}

/* -----------------------------------------------------------------------
 * ARM reset
 * --------------------------------------------------------------------- */

static int arm_reset(struct c985_poc *d, int run)
{
    u32 arm_reset_val, ts;
    unsigned long timeout;

    dev_info(&d->pdev->dev, "arm_reset(run=%d)\n", run);

    if (run == 0) {
        c985_wr(d, REG_ARM_CTRL, 0x00000000);
        c985_wr(d, REG_ARM_BOOT, 0x00000000);
        c985_wr(d, REG_ARM_RESET, 0x00000001);
        c985_wr(d, REG_CLOCK_GATE, 0x00000001);

        ts = readl(d->bar1 + REG_TIMESTAMP);
        c985_wr(d, REG_WATCHDOG_SET, ts + 0xffff);
        c985_wr(d, REG_CLOCK_GATE, 0x00000108);

        msleep(15);

        timeout = jiffies + msecs_to_jiffies(3000);
        for (;;) {
            arm_reset_val = readl(d->bar1 + REG_ARM_RESET);
            if (arm_reset_val == 0)
                break;
            if (time_after(jiffies, timeout)) {
                dev_err(&d->pdev->dev, "ARM reset timeout\n");
                return -ETIMEDOUT;
            }
            udelay(10);
        }

        c985_wr(d, REG_CLOCK_GATE, 0x00000000);
    }

    c985_wr(d, REG_CLK_POWER, 0x00000000);
    c985_wr(d, REG_ARM_BOOT, run ? 0x00000001 : 0x00000000);

    return 0;
}

/* -----------------------------------------------------------------------
 * Firmware upload
 * --------------------------------------------------------------------- */

static int upload_firmware_cpr(struct c985_poc *d, const char *path,
                               const char *label, u32 card_base)
{
    const struct firmware *fw;
    u32 sz4, i, word;
    int ret;

    ret = request_firmware(&fw, path, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load %s: %d\n", path, ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "FW %s: %zu bytes\n", label, fw->size);

    sz4 = ALIGN(fw->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw->size)
            memcpy(&word, fw->data + i, min_t(u32, 4, fw->size - i));
        word = le32_to_cpu(word);

        ret = cpr_write(d, card_base + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "CPR write failed at 0x%x\n", i);
            break;
        }
    }

    release_firmware(fw);
    return ret;
}

/* -----------------------------------------------------------------------
 * Memory init
 * --------------------------------------------------------------------- */

static int codec_initialize_memory(struct c985_poc *d)
{
    u32 local_1c, local_18, local_20, local_24, local_28;
    int ret;

    local_1c = 7;
    local_18 = 2;
    local_20 = 0x20007;

    c985_wr(d, 0x0f14, 0x20007);
    ret = cpr_write(d, 0, local_20);
    if (ret) return ret;

    for (; local_1c > 3; local_1c--) {
        ret = cpr_write(d, 1 << ((local_1c + 6) & 0x1f), local_1c - 1);
        if (ret) return ret;
    }

    ret = cpr_read(d, 0, &local_24);
    if (ret) return ret;
    local_1c = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;
    c985_wr(d, 0x0f14, local_20);

    ret = cpr_write(d, 0, local_18);
    if (ret) return ret;

    for (; local_18 > 1; local_18--) {
        ret = cpr_write(d, 1 << ((local_1c + 0x15) & 0x1f), local_18 - 1);
        if (ret) return ret;
    }

    ret = cpr_read(d, 0, &local_24);
    if (ret) return ret;
    local_18 = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;
    c985_wr(d, 0x0f14, local_20);

    local_28 = readl(d->bar1 + 0x0f1c);
    c985_wr(d, 0x0f1c, local_28 & 0xfffffcff);

    c985_wr(d, 0x0f04, 0x0d03110b);
    c985_wr(d, 0x0f08, 3);
    c985_wr(d, 0x0f40, 2);
    c985_wr(d, 0x0f10, 0x05140080);
    c985_wr(d, 0x0f18, 1);

    msleep(100);
    return 0;
}

/* -----------------------------------------------------------------------
 * AO/VO switches
 * --------------------------------------------------------------------- */

void cqlcodec_ao_switch(struct c985_poc *d, int disable)
{
    u32 val = readl(d->bar1 + REG_AO_VO_CTL);

    if (disable)
        val &= ~AO_ENABLE_BIT;
    else
        val |= AO_ENABLE_BIT;

    c985_wr(d, REG_AO_VO_CTL, val);
    dev_dbg(&d->pdev->dev, "AO: %s\n", disable ? "disabled" : "enabled");
}

void cqlcodec_vo_switch(struct c985_poc *d, int disable)
{
    u32 val = readl(d->bar1 + REG_AO_VO_CTL);

    if (disable)
        val &= ~VO_ENABLE_BIT;
    else
        val |= VO_ENABLE_BIT;

    c985_wr(d, REG_AO_VO_CTL, val);
    dev_dbg(&d->pdev->dev, "VO: %s\n", disable ? "disabled" : "enabled");
}

/* -----------------------------------------------------------------------
 * Load default settings
 * --------------------------------------------------------------------- */

void cqlcodec_load_default_settings(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "CQLCodec: load default settings\n");

    d->enc_reg_message             = ENC_REG_MESSAGE;
    d->enc_reg_system_control      = ENC_REG_SYSTEM_CONTROL;
    d->enc_reg_picture_resolution  = ENC_REG_PICTURE_RESOLUTION;
    d->enc_reg_input_control       = ENC_REG_INPUT_CONTROL;
    d->enc_reg_rate_control        = ENC_REG_RATE_CONTROL;
    d->enc_reg_bit_rate            = ENC_REG_BIT_RATE;
    d->enc_reg_filter_control      = ENC_REG_FILTER_CONTROL;
    d->enc_reg_gop_loop_filter     = ENC_REG_GOP_LOOP_FILTER;
    d->enc_reg_out_pic_resolution  = ENC_REG_OUT_PIC_RESOLUTION;
    d->enc_reg_et_control          = ENC_REG_ET_CONTROL;
    d->enc_reg_block_size          = ENC_REG_BLOCK_SIZE;
    d->enc_reg_audio_control_param = ENC_REG_AUDIO_CONTROL_PARAM;
    d->enc_reg_audio_control_ex    = ENC_REG_AUDIO_CONTROL_EX;

    d->vo_enable       = 1;
    d->vou_mode        = 0;
    d->vou_start_pixel = 1;
    d->vou_start_line  = 0;

    d->ao_enable    = 1;
    d->ao_controls  = DEFAULT_AO_CONTROLS;
    d->aud_controls = DEFAULT_AUD_CONTROLS;

    d->ai_volume = 8;
    d->ao_volume = 8;

    dev_info(&d->pdev->dev, "CQLCodec: defaults loaded\n");
}

/* -----------------------------------------------------------------------
 * Interrupt handler
 * --------------------------------------------------------------------- */

static irqreturn_t cqlcodec_interrupt_handler(int irq, void *dev_id)
{
    struct c985_poc *d = dev_id;
    u32 status;

    status = readl(d->bar1 + REG_ARM_CTRL);
    dev_dbg(&d->pdev->dev, "IRQ: status=0x%08x\n", status);

    return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------
 * Register ISR
 * --------------------------------------------------------------------- */

int cqlcodec_register_isr(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "CQLCodec: register ISR\n");

    ret = request_irq(d->pdev->irq, cqlcodec_interrupt_handler,
                      IRQF_SHARED, DRV_NAME, d);
    if (ret) {
        dev_err(&d->pdev->dev, "Failed to register IRQ %d: %d\n",
                d->pdev->irq, ret);
        d->irq_registered = 0;
        return ret;
    }

    d->irq_registered = 1;
    dev_info(&d->pdev->dev, "CQLCodec: IRQ %d registered\n", d->pdev->irq);

    return 0;
}

/* -----------------------------------------------------------------------
 * GPIO defaults
 * --------------------------------------------------------------------- */

static void gpio_set_defaults(struct c985_poc *d)
{
    c985_wr(d, REG_GPIO_DIR, 0x00000000);
    c985_wr(d, REG_GPIO_VAL, 0x00000000);
}

/* -----------------------------------------------------------------------
 * QPSOS probe
 * --------------------------------------------------------------------- */

static int qpsos_probe_and_init(struct c985_poc *d)
{
    u32 sig0, sig1, version, reg_base;
    int ret;

    ret = cpr_read(d, 0x100, &sig0);
    if (ret) return ret;

    ret = cpr_read(d, 0x104, &sig1);
    if (ret) return ret;

    if (sig0 == 0x534f5351) {
        version = sig1 & 0xff;
        dev_info(&d->pdev->dev, "QPSOS version=%u\n", version);
    } else {
        version = 0;
    }

    reg_base = (version < 3) ? 0x2f2000 : 0x0f2000;

    ret = cpr_write(d, 0x2f1094, 0x00000000);
    if (ret) return ret;

    ret = cpr_write(d, 0x2f1090, 0x00000000);
    if (ret) return ret;

    ret = cpr_write(d, reg_base + 4, 0x00000000);
    if (ret) return ret;

    return 0;
}

/* -----------------------------------------------------------------------
 * Device init
 * --------------------------------------------------------------------- */

int cqlcodec_init_device(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;

    dev_info(&pdev->dev, "cqlcodec_init_device()\n");

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    ret = pci_request_region(pdev, C985_BAR_MMIO, DRV_NAME);
    if (ret)
        return ret;

    d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
    if (!d) {
        ret = -ENOMEM;
        goto err_region;
    }

    d->pdev = pdev;
    d->bar1 = pci_ioremap_bar(pdev, C985_BAR_MMIO);
    if (!d->bar1) {
        ret = -ENOMEM;
        goto err_region;
    }

    pci_set_drvdata(pdev, d);

    d->chip_ver = readl(d->bar1 + REG_CHIP_VER);
    dev_info(&pdev->dev, "chip_ver=0x%08x\n", d->chip_ver);

    cqlcodec_load_default_settings(d);
    c985_dump_regs(d, "initial");

    /* Firmware upload */
    writel(0x00000000, d->bar1 + REG_CLK_POWER);

    ret = upload_firmware_cpr(d, FW_VIDEO, "video", CARD_RAM_VIDEO_BASE);
    if (ret) goto err_out;

    ret = upload_firmware_cpr(d, FW_AUDIO, "audio", CARD_RAM_AUDIO_BASE);
    if (ret) goto err_out;

    /* PLL setup */
    c985_wr(d, PLL4_REG, PLL4_VAL_10020);
    c985_wr(d, PLL5_REG, PLL5_VAL_DEFAULT);
    c985_wr(d, REG_CLK_POWER, 0x00000000);

    /* ARM halt */
    ret = arm_reset(d, 0);
    if (ret) goto err_out;

    /* Memory init */
    ret = qphci_reinit(d);
    if (ret) goto err_out;

    ret = codec_initialize_memory(d);
    if (ret) goto err_out;

    ret = qphci_init_arm_loop(d);
    if (ret) goto err_out;

    /* Switches */
    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);
    gpio_set_defaults(d);

    /* QPSOS */
    ret = qpsos_probe_and_init(d);
    if (ret) goto err_out;

    /* Boot ARM */
    ret = arm_reset(d, 1);
    if (ret) goto err_out;

    msleep(1000);

    /* Register ISR */
    ret = cqlcodec_register_isr(d);
    if (ret)
        dev_warn(&pdev->dev, "ISR registration failed\n");

    /* TI3101 init */
    ti3101_hw_reset(d);

    ret = ti3101_probe(d);
    if (ret)
        dev_warn(&pdev->dev, "TI3101 probe failed\n");

    ret = ti3101_init(d);
    if (ret)
        dev_warn(&pdev->dev, "TI3101 init failed\n");

    /* NUC100 timing */
    nuc100_get_hdmi_timing(d);

    /* Debug */
    c985_dump_regs(d, "post-boot");
    c985_dump_hdmi_presence(d);
    c985_dump_hdmi_mailbox(d, "post-init");

    dev_info(&pdev->dev, "ARM_RESET=0x%08x (0=running)\n",
             readl(d->bar1 + REG_ARM_RESET));

    return 0;

    err_out:
    if (d->bar1)
        iounmap(d->bar1);
    err_region:
    pci_release_region(pdev, C985_BAR_MMIO);
    return ret;
}

/* -----------------------------------------------------------------------
 * Device remove
 * --------------------------------------------------------------------- */

void cqlcodec_remove_device(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "cqlcodec_remove_device()\n");

    if (!d)
        return;

    if (d->irq_registered)
        free_irq(d->pdev->irq, d);

    if (d->bar1)
        iounmap(d->bar1);

    pci_release_region(pdev, C985_BAR_MMIO);
}
