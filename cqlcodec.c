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

static inline u32 c985_rd(struct c985_poc *d, u32 off)
{
    u32 val = readl(d->bar1 + off);
    dev_dbg(&d->pdev->dev, "RD [0x%04x] = 0x%08x\n", off, val);
    return val;
}

/* -----------------------------------------------------------------------
 * Debug helpers
 * --------------------------------------------------------------------- */
static void dump_full_state(struct c985_poc *d, const char *tag)
{
    dev_info(&d->pdev->dev, "========== %s ==========\n", tag);
    dev_info(&d->pdev->dev, "[%s] IRQ: 0x04=0x%08x 0x24=0x%08x 0x4000=0x%08x 0x4030=0x%08x\n",
             tag,
             readl(d->bar1 + 0x04),
             readl(d->bar1 + 0x24),
             readl(d->bar1 + 0x4000),
             readl(d->bar1 + 0x4030));
    dev_info(&d->pdev->dev, "[%s] HCI: 0x800=0x%08x 0x804=0x%08x 0x80C=0x%08x\n",
             tag,
             readl(d->bar1 + 0x800),
             readl(d->bar1 + 0x804),
             readl(d->bar1 + 0x80C));
    dev_info(&d->pdev->dev, "[%s] MAILBOX: 0x6C8=0x%08x 0x6CC=0x%08x 0x6FC=0x%08x\n",
             tag,
             readl(d->bar1 + 0x6C8),
             readl(d->bar1 + 0x6CC),
             readl(d->bar1 + 0x6FC));
    dev_info(&d->pdev->dev, "[%s] ARM: RESET=0x%08x BOOT=0x%08x CTRL=0x%08x\n",
             tag,
             readl(d->bar1 + 0x800),
             readl(d->bar1 + 0x80C),
             readl(d->bar1 + 0x00));
}

/* -----------------------------------------------------------------------
 * ARM reset — matches DM_ResetArm exactly
 * --------------------------------------------------------------------- */
static int arm_reset(struct c985_poc *d, int run)
{
    u32 arm_reset_val, ts;
    unsigned long timeout;

    dev_info(&d->pdev->dev, "arm_reset(run=%d)\n", run);

    if (run == 0) {
        c985_wr(d, 0x00, 0x00000000);
        c985_wr(d, 0x80C, 0x00000000);
        c985_wr(d, 0x800, 0x00000001);
        c985_wr(d, 0x10, 0x00000001);

        ts = c985_rd(d, 0x1C);
        c985_wr(d, 0x18, ts + 0xffff);
        c985_wr(d, 0x10, 0x00000108);

        msleep(15);

        timeout = jiffies + msecs_to_jiffies(3000);
        for (;;) {
            arm_reset_val = c985_rd(d, 0x800);
            if (arm_reset_val == 0)
                break;
            if (time_after(jiffies, timeout)) {
                dev_err(&d->pdev->dev, "ARM reset timeout (0x800=0x%08x)\n",
                        arm_reset_val);
                return -ETIMEDOUT;
            }
            udelay(10);
        }

        c985_wr(d, 0x10, 0x00000000);

        /* Post-reset cleanup */
        c985_wr(d, 0x6CC, 0x00000000);
        c985_wr(d, 0x80C, 0x00000000);

        dev_info(&d->pdev->dev, "ARM halted\n");
    } else {
        /* Start ARM */
        c985_wr(d, 0x6CC, 0x00000000);
        c985_wr(d, 0x80C, 0x00000001);

        dev_info(&d->pdev->dev, "=== ARM BOOT MONITOR ===\n");
        {
            int i;
            for (i = 0; i < 30; i++) {
                u32 arm_run = readl(d->bar1 + 0x80C);
                u32 r6c8 = readl(d->bar1 + 0x6C8);
                u32 r6cc = readl(d->bar1 + 0x6CC);
                u32 r800 = readl(d->bar1 + 0x800);
                u32 r804 = readl(d->bar1 + 0x804);

                dev_info(&d->pdev->dev,
                         "BOOT[%02d]: 80C=%d 6C8=0x%08x 6CC=0x%08x 800=0x%08x 804=0x%08x\n",
                         i, arm_run, r6c8, r6cc, r800, r804);

                if (r6c8 & 1) {
                    dev_info(&d->pdev->dev, "*** ARM sent boot message! ***\n");
                    dev_info(&d->pdev->dev, "  6B0=0x%08x 6B4=0x%08x 6B8=0x%08x 6BC=0x%08x\n",
                             readl(d->bar1 + 0x6B0),
                             readl(d->bar1 + 0x6B4),
                             readl(d->bar1 + 0x6B8),
                             readl(d->bar1 + 0x6BC));
                    c985_wr(d, 0x6C8, 0);
                }

                if (arm_run == 1 && i >= 5)
                    break;

                msleep(100);
            }
        }

        dump_full_state(d, "POST-ARM-BOOT");
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * Firmware upload via CPR
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

    dev_info(&d->pdev->dev, "=== MEMORY INIT START ===\n");

    local_1c = 7;
    local_18 = 2;
    local_20 = 0x20007;

    dev_info(&d->pdev->dev, "Initial: local_1c=%u local_18=%u local_20=0x%08x\n",
             local_1c, local_18, local_20);

    /* Write initial value to 0xf14 */
    dev_info(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    c985_wr(d, 0x0f14, 0x20007);

    dev_info(&d->pdev->dev, "CPR write addr=0x0 val=0x%08x\n", local_20);
    ret = cpr_write(d, 0, local_20);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR write #1 failed\n");
        return ret;
    }

    /* Row address detection loop */
    dev_info(&d->pdev->dev, "=== ROW ADDRESS DETECTION ===\n");
    for (; local_1c > 3; local_1c--) {
        u32 addr = 1 << ((local_1c + 6) & 0x1f);
        u32 val = local_1c - 1;

        dev_info(&d->pdev->dev, "Row loop: local_1c=%u addr=0x%08x val=%u\n",
                 local_1c, addr, val);

        ret = cpr_write(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write row failed at local_1c=%u\n", local_1c);
            return ret;
        }
    }

    dev_info(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = cpr_read(d, 0, &local_24);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR read #1 failed\n");
        return ret;
    }

    local_1c = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;

    dev_info(&d->pdev->dev, "Row detect result: read=0x%08x local_1c=%u local_20=0x%08x\n",
             local_24, local_1c, local_20);
    dev_info(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    c985_wr(d, 0x0f14, local_20);

    /* Column address detection */
    dev_info(&d->pdev->dev, "=== COLUMN ADDRESS DETECTION ===\n");
    dev_info(&d->pdev->dev, "CPR write addr=0x0 val=0x%08x\n", local_18);
    ret = cpr_write(d, 0, local_18);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR write #2 failed\n");
        return ret;
    }

    for (; local_18 > 1; local_18--) {
        u32 addr = 1 << ((local_1c + 0x15) & 0x1f);
        u32 val = local_18 - 1;

        dev_info(&d->pdev->dev, "Col loop: local_18=%u addr=0x%08x val=%u\n",
                 local_18, addr, val);

        ret = cpr_write(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write col failed at local_18=%u\n", local_18);
            return ret;
        }
    }

    dev_info(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = cpr_read(d, 0, &local_24);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR read #2 failed\n");
        return ret;
    }

    local_18 = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;

    dev_info(&d->pdev->dev, "Col detect result: read=0x%08x local_18=%u local_20=0x%08x\n",
             local_24, local_18, local_20);
    dev_info(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    c985_wr(d, 0x0f14, local_20);

    /* Final register configuration */
    dev_info(&d->pdev->dev, "=== FINAL REGISTER CONFIG ===\n");
    local_28 = c985_rd(d, 0x0f1c);
    dev_info(&d->pdev->dev, "Read 0xf1c = 0x%08x\n", local_28);
    dev_info(&d->pdev->dev, "Writing 0xf1c = 0x%08x\n", local_28 & 0xfffffcff);
    c985_wr(d, 0x0f1c, local_28 & 0xfffffcff);

    dev_info(&d->pdev->dev, "Writing memory controller registers:\n");
    dev_info(&d->pdev->dev, "  0xf04 = 0x0d03110b\n");
    c985_wr(d, 0x0f04, 0x0d03110b);

    dev_info(&d->pdev->dev, "  0xf08 = 0x00000003\n");
    c985_wr(d, 0x0f08, 3);

    dev_info(&d->pdev->dev, "  0xf40 = 0x00000002\n");
    c985_wr(d, 0x0f40, 2);

    dev_info(&d->pdev->dev, "  0xf10 = 0x05140080\n");
    c985_wr(d, 0x0f10, 0x05140080);

    dev_info(&d->pdev->dev, "  0xf18 = 0x00000001\n");
    c985_wr(d, 0x0f18, 1);

    dev_info(&d->pdev->dev, "Waiting 100ms for memory stabilization...\n");
    msleep(100);

    dev_info(&d->pdev->dev, "=== MEMORY INIT COMPLETE ===\n");
    return 0;
}
/* -----------------------------------------------------------------------
 * AO/VO switches
 * --------------------------------------------------------------------- */
void cqlcodec_ao_switch(struct c985_poc *d, int disable)
{
    u32 val = c985_rd(d, REG_AO_VO_CTL);
    if (disable)
        val &= ~AO_ENABLE_BIT;
    else
        val |= AO_ENABLE_BIT;
    c985_wr(d, REG_AO_VO_CTL, val);
}

void cqlcodec_vo_switch(struct c985_poc *d, int disable)
{
    u32 val = c985_rd(d, REG_AO_VO_CTL);
    if (disable)
        val &= ~VO_ENABLE_BIT;
    else
        val |= VO_ENABLE_BIT;
    c985_wr(d, REG_AO_VO_CTL, val);
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
    u32 pci_status, hci_status;
    int handled = 0;

    pci_status = readl(d->bar1 + 0x4030);

    if (!(pci_status & 0x40010000))
        return IRQ_NONE;

    hci_status = readl(d->bar1 + 0x804);

    dev_info(&d->pdev->dev, "IRQ: pci=0x%08x hci=0x%08x\n", pci_status, hci_status);

    if ((pci_status & 0x10000) || (hci_status & BIT(16))) {
        u32 msg_status = readl(d->bar1 + 0x6C8);
        u32 msg_data   = readl(d->bar1 + 0x6CC);

        if (msg_status != 0) {
            u8 cmd   = msg_status & 0xFF;
            u16 task = (msg_status >> 16) & 0xFFFF;

            dev_info(&d->pdev->dev,
                     "IRQ: cmd=0x%02x task=%u st=0x%08x dat=0x%08x\n",
                     cmd, task, msg_status, msg_data);
            dev_info(&d->pdev->dev,
                     "IRQ: 6B0=0x%08x 6B4=0x%08x 6B8=0x%08x 6BC=0x%08x\n",
                     readl(d->bar1 + 0x6B0), readl(d->bar1 + 0x6B4),
                     readl(d->bar1 + 0x6B8), readl(d->bar1 + 0x6BC));
            dev_info(&d->pdev->dev,
                     "IRQ: 6F0=0x%08x 6F4=0x%08x 6F8=0x%08x 6FC=0x%08x\n",
                     readl(d->bar1 + 0x6F0), readl(d->bar1 + 0x6F4),
                     readl(d->bar1 + 0x6F8), readl(d->bar1 + 0x6FC));

            switch (cmd) {
                case 0x01:
                    dev_info(&d->pdev->dev, "IRQ: Encoder STARTED\n");
                    break;
                case 0x02:
                    dev_info(&d->pdev->dev, "IRQ: Encoder STOPPED\n");
                    break;
                case 0x06:
                    dev_info(&d->pdev->dev, "IRQ: Config updated\n");
                    break;
                case 0x10:
                    dev_info(&d->pdev->dev, "IRQ: VIU ack\n");
                    break;
                case 0x40:
                    dev_info(&d->pdev->dev, "IRQ: VIDEO FRAME seq=%u\n", d->sequence++);
                    break;
                case 0x41:
                    dev_info(&d->pdev->dev, "IRQ: AUDIO FRAME\n");
                    break;
                case 0x80:
                    dev_info(&d->pdev->dev, "IRQ: ERROR 0x%08x\n", msg_data);
                    break;
                case 0xF1:
                    dev_info(&d->pdev->dev, "IRQ: SystemOpen ack\n");
                    break;
                case 0xF2:
                    dev_info(&d->pdev->dev, "IRQ: SystemLink ack\n");
                    break;
                default:
                    dev_info(&d->pdev->dev, "IRQ: cmd=0x%02x\n", cmd);
                    break;
            }

            writel(0, d->bar1 + 0x6C8);
        }

        writel(BIT(16), d->bar1 + 0x804);
        handled = 1;
    }

    if (hci_status & BIT(17)) {
        dev_info(&d->pdev->dev, "IRQ: DMA read done\n");
        writel(BIT(17), d->bar1 + 0x804);
        handled = 1;
    }

    if (hci_status & BIT(18)) {
        dev_info(&d->pdev->dev, "IRQ: DMA write done\n");
        writel(BIT(18), d->bar1 + 0x804);
        handled = 1;
    }

    if (pci_status & 0x40000000)
        writel(0x40000000, d->bar1 + 0x4030);
    if (pci_status & 0x10000)
        writel(0x10000, d->bar1 + 0x4030);

    return handled ? IRQ_HANDLED : IRQ_NONE;
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

    pci_set_master(pdev);

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

    d->chip_ver = c985_rd(d, REG_CHIP_VER);
    dev_info(&pdev->dev, "chip_ver=0x%08x\n", d->chip_ver);

    dump_full_state(d, "INITIAL");

    cqlcodec_load_default_settings(d);

    /* NOTE: Do NOT call codec_initialize_memory() here.
     * It will be called by cqlcodec_fw_download() after
     * arm_reset(0) and qphci_reinit() in the correct order. */

    ret = qphci_init_arm_loop(d);
    if (ret)
        goto err_out;

    gpio_set_defaults(d);

    ret = cqlcodec_register_isr(d);
    if (ret)
        dev_warn(&pdev->dev, "ISR registration failed\n");

    {
        u32 status = c985_rd(d, REG_PCI_INT_STATUS);
        c985_wr(d, REG_PCI_INT_STATUS, status | 0x70000);
        dev_info(&pdev->dev, "cleared pending IRQ status=0x%08x\n", status);
    }

    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);

    c985_wr(d, 0x4000, c985_rd(d, 0x4000) | 1);

    dump_full_state(d, "AFTER-INIT");
    return 0;

    err_out:
    if (d->irq_registered)
        free_irq(d->pdev->irq, d);
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
    u32 val, reg0, arm_reset_val;
    unsigned long timeout;

    dev_info(&pdev->dev, "cqlcodec_remove_device()\n");

    if (!d)
        return;

    if (d->bar1) {
        /* 1. Disable interrupts first */
        val = readl(d->bar1 + 0x4000);
        writel(val & ~1, d->bar1 + 0x4000);
        writel(0x7FFFFFFF, d->bar1 + 0x4030);
        writel(0x00070000, d->bar1 + 0x804);
    }

    if (d->irq_registered) {
        free_irq(d->pdev->irq, d);
        d->irq_registered = 0;
    }

    if (d->bar1) {
        /* 2. CQLCodec_PowerDown sequence */
        dev_info(&pdev->dev, "CQLCodec_PowerDown sequence\n");

        /* Disable ARM via HCI */
        writel(0x00000000, d->bar1 + 0x80C);

        /* Clear mailbox */
        writel(0x00000000, d->bar1 + 0x6CC);
        writel(0x00000000, d->bar1 + 0x6C8);

        /* Read reg 0, preserve top 3 bits, apply reset */
        reg0 = readl(d->bar1 + 0x00);
        writel(reg0 & 0xE0000000, d->bar1 + 0x00);
        writel((reg0 & 0xE0000000) | 0x1FFFFFF, d->bar1 + 0x00);

        /* 3. QPHCI_PowerDown sequence */
        dev_info(&pdev->dev, "QPHCI_PowerDown sequence\n");

        /* DDR into power-down/reset mode - THIS IS CRITICAL */
        writel(0x00000004, d->bar1 + 0x0f1c);

        /* Pad control - tri-state */
        val = readl(d->bar1 + 0x50);
        writel(val | 0x106, d->bar1 + 0x50);
        dev_info(&pdev->dev, "Pad control: 0x%08x -> 0x%08x\n", val, val | 0x106);

        /* Final control register reset */
        writel(0x00000000, d->bar1 + 0x00);
        writel(0x01FFFFFF, d->bar1 + 0x00);

        msleep(10);

        /* 4. Now do ARM halt sequence */
        dev_info(&pdev->dev, "ARM halt\n");
        writel(0x00000000, d->bar1 + 0x80C);
        writel(0x00000001, d->bar1 + 0x800);
        writel(0x00000001, d->bar1 + 0x10);
        {
            u32 ts = readl(d->bar1 + 0x1C);
            writel(ts + 0xFFFF, d->bar1 + 0x18);
        }
        writel(0x00000108, d->bar1 + 0x10);
        msleep(15);

        timeout = jiffies + msecs_to_jiffies(3000);
        while (readl(d->bar1 + 0x800) != 0) {
            if (time_after(jiffies, timeout)) {
                dev_err(&pdev->dev, "ARM reset timeout\n");
                break;
            }
            udelay(10);
        }
        writel(0x00000000, d->bar1 + 0x10);

        dev_info(&pdev->dev, "hardware shutdown complete\n");
        iounmap(d->bar1);
        d->bar1 = NULL;
    }

    pci_release_region(pdev, C985_BAR_MMIO);
}
/* -----------------------------------------------------------------------
 * Firmware download — CORRECTED
 *
 * Windows driver order (from CQLCodec_FWDownloadAll):
 *   ResetArm(0)
 *   QPHCI_ReInit
 *   InitializeMemory
 *   AO/VO switch, GPIO
 *   delay 50ms
 *   Read reg 0x00, clear bit 13, write back (for audio FW)
 *   Upload AUDIO FW to 0x100000 (param_5=0x100000, do_reset=0)
 *   delay 1ms
 *   Upload VIDEO FW to 0x000000 (param_5=0, do_reset=0)
 *   - Reads QPSOS from firmware BUFFER (host memory), not card RAM
 *   - Writes fw_fixed_mode, fw_int_mode to card RAM via MemoryWrite
 *   - Sets PLLs
 *   - Clears 0x6CC
 *   delay 500us
 *   ResetArm(1)
 *   delay 150ms
 * --------------------------------------------------------------------- */
int cqlcodec_fw_download(struct c985_poc *d, int do_reset)
{
    const struct firmware *fw_vid = NULL;
    const struct firmware *fw_aud = NULL;
    u32 qpsos_version = 0;
    u32 reg_base;
    u32 sz4, i, word;
    u32 test_val;
    int ret;

    dev_info(&d->pdev->dev,
             "================ FW DOWNLOAD (reset=%d) ================\n",
             do_reset);

    dump_full_state(d, "FW-DOWNLOAD-START");

    /* Load firmware files */
    ret = request_firmware(&fw_vid, FW_VIDEO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load video FW: %d\n", ret);
        return ret;
    }
    dev_info(&d->pdev->dev, "FW video: %zu bytes\n", fw_vid->size);

    ret = request_firmware(&fw_aud, FW_AUDIO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load audio FW: %d\n", ret);
        release_firmware(fw_vid);
        return ret;
    }
    dev_info(&d->pdev->dev, "FW audio: %zu bytes\n", fw_aud->size);

    if (do_reset) {
        /* Step 1: ARM reset (halt) */
        dev_info(&d->pdev->dev, "STEP: ARM reset\n");
        ret = arm_reset(d, 0);
        if (ret)
            goto out;

        /* Step 2: QPHCI_PowerUp sequence - restore from power-down state */
        dev_info(&d->pdev->dev, "STEP: Power-up sequence\n");
        {
            u32 pad_ctl = c985_rd(d, 0x50);
            dev_info(&d->pdev->dev, "Pad control before: 0x%08x\n", pad_ctl);
            pad_ctl &= 0xFFFFFEFF;  /* Clear bit 8 */
            c985_wr(d, 0x50, pad_ctl);
            dev_info(&d->pdev->dev, "Pad control after:  0x%08x\n", pad_ctl);
        }

        /* Restore DDR controller to power-on default BEFORE memory init */
        dev_info(&d->pdev->dev, "DDR 0xf1c = 0x00000f00 (power-on default)\n");
        c985_wr(d, 0x0f1c, 0x00000f00);

        /* Step 3: QPHCI reinit */
        dev_info(&d->pdev->dev, "STEP: QPHCI reinit\n");
        ret = qphci_reinit(d);
        if (ret)
            goto out;

        /* Step 4: Memory init */
        dev_info(&d->pdev->dev, "STEP: Memory init\n");
        ret = codec_initialize_memory(d);
        if (ret)
            goto out;

        /* Verify CPR is working after memory init */
        dev_info(&d->pdev->dev, "=== CPR CHECK: AFTER MEMORY INIT ===\n");
        cpr_write(d, 0, 0xAAAAAAAA);
        cpr_read(d, 0, &test_val);
        dev_info(&d->pdev->dev,
                 "CPR[0x0] = 0x%08x (expect 0xAAAAAAAA) %s\n",
                 test_val, test_val == 0xAAAAAAAA ? "OK" : "FAIL");

        cpr_write(d, 0x1000, 0x55555555);
        cpr_read(d, 0x1000, &test_val);
        dev_info(&d->pdev->dev,
                 "CPR[0x1000] = 0x%08x (expect 0x55555555) %s\n",
                 test_val, test_val == 0x55555555 ? "OK" : "FAIL");

        /* Dump memory controller state */
        dev_info(&d->pdev->dev,
                 "MEMCTL: 0xf04=0x%08x 0xf08=0x%08x 0xf10=0x%08x\n",
                 readl(d->bar1 + 0xf04),
                 readl(d->bar1 + 0xf08),
                 readl(d->bar1 + 0xf10));
        dev_info(&d->pdev->dev,
                 "MEMCTL: 0xf14=0x%08x 0xf18=0x%08x 0xf1c=0x%08x 0xf40=0x%08x\n",
                 readl(d->bar1 + 0xf14),
                 readl(d->bar1 + 0xf18),
                 readl(d->bar1 + 0xf1c),
                 readl(d->bar1 + 0xf40));

        /* Step 5: AO/VO and GPIO setup */
        cqlcodec_ao_switch(d, !d->ao_enable);
        cqlcodec_vo_switch(d, !d->vo_enable);
        gpio_set_defaults(d);

        msleep(50);
    }

    /* Audio FW prep - clear bit 13 of reg 0x00 */
    if (do_reset) {
        u32 reg0 = c985_rd(d, 0x00);
        c985_wr(d, 0x00, reg0 & ~BIT(13));
        msleep(1);
    }

    /* Upload AUDIO FW */
    dev_info(&d->pdev->dev,
             "=== UPLOAD AUDIO FW @ 0x%08x ===\n",
             CARD_RAM_AUDIO_BASE);
    sz4 = ALIGN(fw_aud->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw_aud->size)
            memcpy(&word, fw_aud->data + i,
                   min_t(u32, 4, fw_aud->size - i));
            word = le32_to_cpu(word);
        ret = cpr_write(d, CARD_RAM_AUDIO_BASE + i, word);
        if (ret) {
            dev_err(&d->pdev->dev,
                    "AUDIO CPR write failed at 0x%x\n", i);
            goto out;
        }
    }

    msleep(1);

    /* QPSOS from FW buffer */
    if (fw_vid->size > 0x108) {
        u32 sig;

        memcpy(&sig, fw_vid->data + 0x100, 4);
        sig = le32_to_cpu(sig);
        dev_info(&d->pdev->dev,
                 "QPSOS sig from FW buffer = 0x%08x\n", sig);
        if (sig == 0x534f5351) {
            u16 ver;

            memcpy(&ver, fw_vid->data + 0x106, 2);
            ver = le16_to_cpu(ver);
            qpsos_version = ver;
            dev_info(&d->pdev->dev,
                     "QPSOS version=%u\n", qpsos_version);
        }
    }

    reg_base = (qpsos_version < 3) ? 0x2f2000 : 0x0f2000;
    dev_info(&d->pdev->dev, "QPSOS reg_base=0x%06x\n", reg_base);

    cpr_write(d, 0x2f1094, 0);
    cpr_write(d, 0x2f1090, 0);
    cpr_write(d, reg_base + 4, 0);

    /* PLL setup */
    dev_info(&d->pdev->dev,
             "PLL setup: chip_ver=0x%08x\n", d->chip_ver);
    if (d->chip_ver == 0x10020)
        c985_wr(d, PLL4_REG, PLL4_VAL_10020);
    else
        c985_wr(d, PLL4_REG, 0x20236);
    c985_wr(d, PLL5_REG, PLL5_VAL_DEFAULT);

    c985_wr(d, 0x6CC, 0);

    /* Upload VIDEO FW */
    dev_info(&d->pdev->dev,
             "=== UPLOAD VIDEO FW @ 0x%08x ===\n",
             CARD_RAM_VIDEO_BASE);
    sz4 = ALIGN(fw_vid->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw_vid->size)
            memcpy(&word, fw_vid->data + i,
                   min_t(u32, 4, fw_vid->size - i));
            word = le32_to_cpu(word);
        ret = cpr_write(d, CARD_RAM_VIDEO_BASE + i, word);
        if (ret) {
            dev_err(&d->pdev->dev,
                    "VIDEO CPR write failed at 0x%x\n", i);
            goto out;
        }
    }

    /* Verify firmware upload */
    dev_info(&d->pdev->dev, "=== FW VERIFY ===\n");
    {
        int mismatches = 0;
        u32 card_val, host_val;
        int j;

        /* Check first few words of video FW */
        for (j = 0; j < 32; j += 4) {
            cpr_read(d, CARD_RAM_VIDEO_BASE + j, &card_val);
            host_val = 0;
            if (j < fw_vid->size)
                memcpy(&host_val, fw_vid->data + j,
                       min_t(size_t, 4, fw_vid->size - j));
                host_val = le32_to_cpu(host_val);

            if (card_val != host_val) {
                dev_info(&d->pdev->dev,
                         "[0x%04x]: card=0x%08x fw=0x%08x MISMATCH\n",
                         j, card_val, host_val);
                mismatches++;
            } else {
                dev_info(&d->pdev->dev,
                         "[0x%04x]: card=0x%08x fw=0x%08x OK\n",
                         j, card_val, host_val);
            }
        }

        /* Check first few words of audio FW */
        for (j = 0; j < 16; j += 4) {
            cpr_read(d, CARD_RAM_AUDIO_BASE + j, &card_val);
            host_val = 0;
            if (j < fw_aud->size)
                memcpy(&host_val, fw_aud->data + j,
                       min_t(size_t, 4, fw_aud->size - j));
                host_val = le32_to_cpu(host_val);

            if (card_val != host_val) {
                dev_info(&d->pdev->dev,
                         "AUD[0x%06x]: card=0x%08x fw=0x%08x MISMATCH\n",
                         CARD_RAM_AUDIO_BASE + j, card_val, host_val);
                mismatches++;
            } else {
                dev_info(&d->pdev->dev,
                         "AUD[0x%06x]: card=0x%08x fw=0x%08x OK\n",
                         CARD_RAM_AUDIO_BASE + j, card_val, host_val);
            }
        }

        dev_info(&d->pdev->dev,
                 "FW verify: %d mismatches\n", mismatches);
    }

    dump_full_state(d, "AFTER-FW-UPLOAD");

    if (do_reset) {
        /* Start ARM */
        ret = arm_reset(d, 1);
        if (ret)
            goto out;

        msleep(150);
        dump_full_state(d, "POST-ARM-BOOT");
    }

    out:
    if (fw_vid)
        release_firmware(fw_vid);
    if (fw_aud)
        release_firmware(fw_aud);

    return ret;
}
