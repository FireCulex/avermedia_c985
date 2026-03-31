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
#include "qpfwapi.h"

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
    writel(local_20, d->bar1 + 0x0f14);

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
    writel(local_20, d->bar1 + 0x0f14);

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
    writel(local_20, d->bar1 + 0x0f14);

    /* Final register configuration */
    dev_info(&d->pdev->dev, "=== FINAL REGISTER CONFIG ===\n");
    local_28 = readl(d->bar1 + 0x0f1c);
    dev_info(&d->pdev->dev, "Read 0xf1c = 0x%08x\n", local_28);
    dev_info(&d->pdev->dev, "Writing 0xf1c = 0x%08x\n", local_28 & 0xfffffcff);
    writel(local_28 & 0xfffffcff, d->bar1 + 0x0f1c);

    dev_info(&d->pdev->dev, "Writing memory controller registers:\n");
    dev_info(&d->pdev->dev, "  0xf04 = 0x0d03110b\n");
    writel(0x0d03110b, d->bar1 + 0x0f04);

    dev_info(&d->pdev->dev, "  0xf08 = 0x00000003\n");
    writel(0x00000003, d->bar1 + 0x0f08);

    dev_info(&d->pdev->dev, "  0xf40 = 0x00000002\n");
    writel(0x00000002, d->bar1 + 0x0f40);

    dev_info(&d->pdev->dev, "  0xf10 = 0x05140080\n");
    writel(0x05140080, d->bar1 + 0x0f10);

    dev_info(&d->pdev->dev, "  0xf18 = 0x00000001\n");
    writel(0x00000001, d->bar1 + 0x0f18);

    dev_info(&d->pdev->dev, "Waiting 100ms for memory stabilization...\n");
    msleep(100);

    /* CPR verification */
    dev_info(&d->pdev->dev, "=== CPR CHECK: AFTER MEMORY INIT ===\n");
    {
        u32 test_val;
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
    }

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
/* Updated interrupt handler using the new functions: */

static irqreturn_t cqlcodec_interrupt_handler(int irq, void *dev_id)
{
    struct c985_poc *d = dev_id;
    u32 int_status, hci_status, mm_status;
    u32 clear_mask = 0;

    /* Read interrupt status registers */
    int_status = readl(d->bar1 + REG_INT_STATUS);
    hci_status = readl(d->bar1 + REG_HCI_INT_CTRL);
    mm_status  = readl(d->bar1 + REG_HOST_TO_ARM_TRIG);

    /* Not our interrupt */
    if (!int_status && !hci_status && !(mm_status & 0x1000000))
        return IRQ_NONE;

    if (printk_ratelimit())
        dev_info(&d->pdev->dev,
                 "IRQ: INT_STATUS=0x%08x HCI=0x%08x MM=0x%08x\n",
                 int_status, hci_status, mm_status);

        /* Clear mailbox interrupt */
        if (mm_status & 0x1000000) {
            mm_clear_interrupt(d);
        }

        /* HCI ARM message interrupt */
        if (hci_status & HCI_INT_ARM_MSG) {
            clear_mask |= DM_INT_ARM_MSG;

            u32 arm_response = readl(d->bar1 + REG_ARM_RESPONSE);
            u32 arm_data     = readl(d->bar1 + REG_ARM_RESP_DATA);

            if (arm_response) {
                u8  cmd  = arm_response & 0xFF;
                u16 task = (arm_response >> 16) & 0xFFFF;

                dev_info(&d->pdev->dev,
                         "IRQ: cmd=0x%02x task=%u response=0x%08x data=0x%08x\n",
                         cmd, task, arm_response, arm_data);

                switch (cmd) {
                    case ARM_CMD_START:
                        dev_info(&d->pdev->dev, "IRQ: Encoder STARTED\n");
                        break;
                    case ARM_CMD_STOP:
                        dev_info(&d->pdev->dev, "IRQ: Encoder STOPPED\n");
                        break;
                    case ARM_CMD_UPDATE_CONFIG:
                        dev_info(&d->pdev->dev, "IRQ: Config updated\n");
                        break;
                    case ARM_CMD_SET_VIU_SYNC:
                        dev_info(&d->pdev->dev, "IRQ: VIU ack\n");
                        break;
                    case ARM_CMD_VIDEO_FRAME:
                        dev_info(&d->pdev->dev,
                                 "IRQ: VIDEO FRAME seq=%u\n",
                                 d->sequence++);
                        break;
                    case ARM_CMD_AUDIO_FRAME:
                        dev_info(&d->pdev->dev, "IRQ: AUDIO FRAME\n");
                        break;
                    case ARM_CMD_ERROR:
                        dev_info(&d->pdev->dev,
                                 "IRQ: ERROR 0x%08x\n",
                                 arm_data);
                        break;
                    case ARM_CMD_SYSTEM_OPEN:
                        dev_info(&d->pdev->dev, "IRQ: SystemOpen ack\n");
                        break;
                    case ARM_CMD_SYSTEM_LINK:
                        dev_info(&d->pdev->dev, "IRQ: SystemLink ack\n");
                        break;
                    default:
                        dev_info(&d->pdev->dev,
                                 "IRQ: Unknown cmd=0x%02x\n",
                                 cmd);
                        break;
                }

                /* ACK ARM message */
                writel(0, d->bar1 + REG_ARM_RESPONSE);
            }
        }

        if (hci_status & HCI_INT_DMA_READ) {
            dev_info(&d->pdev->dev, "IRQ: DMA read complete\n");
            clear_mask |= DM_INT_DMA_READ;
        }

        if (hci_status & HCI_INT_DMA_WRITE) {
            dev_info(&d->pdev->dev, "IRQ: DMA write complete\n");
            clear_mask |= DM_INT_DMA_WRITE;
        }

        /* Clear HCI interrupts */
        if (clear_mask) {
            dm_clear_interrupt(d, clear_mask);
        }

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

    dev_info(&pdev->dev, "=== FIRST REGISTER READ ===\n");
    dev_info(&pdev->dev, "reg 0x00 = 0x%08x\n", readl(d->bar1 + 0x00));
    dev_info(&pdev->dev, "reg 0xf14 = 0x%08x\n", readl(d->bar1 + 0x0f14));
    dev_info(&pdev->dev, "reg 0xf18 = 0x%08x\n", readl(d->bar1 + 0x0f18));
    dev_info(&pdev->dev, "reg 0xf1c = 0x%08x\n", readl(d->bar1 + 0x0f1c));

    /* Load default settings */
    cqlcodec_load_default_settings(d);

    /* Step 1: QPHCI_Init - includes PowerUp, memory windows, etc */
    ret = qphci_init(d);
    if (ret)
        goto err_out;

    /* Step 2: CQLCodec_InitializeMemory */
    ret = codec_initialize_memory(d);
    if (ret)
        goto err_out;

    /* Step 3: QPHCI_InitArmLoop - write branch-to-self at reset vector */
    ret = qphci_init_arm_loop(d);
    if (ret)
        goto err_out;

    /* Step 4: SetGPIODefaults */
    gpio_set_defaults(d);

    /* Step 5: Register ISR */
    ret = cqlcodec_register_isr(d);
    if (ret) {
        dev_warn(&pdev->dev, "ISR registration failed\n");
        goto err_out;
    }

    /* Step 6: AO/VO switches */
    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);

    dev_info(&pdev->dev, "CQLCodec init complete\n");
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

    dev_info(&pdev->dev, "cqlcodec_remove_device()\n");

    if (!d)
        return;

    if (d->bar1) {
        /* Disable interrupt generation */
        u32 val = readl(d->bar1 + 0x4000);
        writel(val & ~1, d->bar1 + 0x4000);

        /* Clear pending status */
        writel(readl(d->bar1 + REG_PCI_INT_STATUS), d->bar1 + REG_PCI_INT_STATUS);
        writel(0x00070000, d->bar1 + 0x804);
        writel(0, d->bar1 + 0x6C8);
        writel(0, d->bar1 + 0x6CC);
    }

    if (d->irq_registered) {
        free_irq(d->pdev->irq, d);
        d->irq_registered = 0;
    }

    if (d->bar1) {
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
    u32 reg0;
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
        /* Step 1: ResetArm(0) - halt ARM */
        ret = dm_reset_arm(d, 0);
        if (ret)
            goto out;

        /* Step 2: QPHCI_ReInit */
        ret = qphci_reinit(d);
        if (ret)
            goto out;

        /* Step 3: InitializeMemory */
        ret = codec_initialize_memory(d);
        if (ret)
            goto out;

        /* Step 4-6: AO/VO/GPIO */
        cqlcodec_ao_switch(d, !d->ao_enable);
        cqlcodec_vo_switch(d, !d->vo_enable);
        gpio_set_defaults(d);

        /* Step 7: 50ms delay */
        msleep(50);

        /* Step 8-9: Read reg 0, clear bit 13, write back */
        reg0 = readl(d->bar1 + 0x00);
        writel(reg0 & 0xFFFFDFFF, d->bar1 + 0x00);

        /* Step 10: 1us delay */
        udelay(1);
    }

    /* Step 11: Upload AUDIO FW @ 0x100000 */
    dev_info(&d->pdev->dev, "=== UPLOAD AUDIO FW @ 0x%08x ===\n", CARD_RAM_AUDIO_BASE);
    sz4 = ALIGN(fw_aud->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw_aud->size)
            memcpy(&word, fw_aud->data + i, min_t(u32, 4, fw_aud->size - i));
        word = le32_to_cpu(word);
        ret = cpr_write(d, CARD_RAM_AUDIO_BASE + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "AUDIO CPR write failed at 0x%x\n", i);
            goto out;
        }
    }

    /* Step 12: 1ms delay */
    msleep(1);

    /* Video FW setup - QPSOS version detection */
    if (fw_vid->size > 0x108) {
        u32 sig;
        memcpy(&sig, fw_vid->data + 0x100, 4);
        sig = le32_to_cpu(sig);
        dev_info(&d->pdev->dev, "QPSOS sig from FW buffer = 0x%08x\n", sig);
        if (sig == 0x534f5351) {
            u16 ver;
            memcpy(&ver, fw_vid->data + 0x106, 2);
            qpsos_version = le16_to_cpu(ver);
            dev_info(&d->pdev->dev, "QPSOS version=%u\n", qpsos_version);
        }
    }

    reg_base = (qpsos_version < 3) ? 0x2f2000 : 0x0f2000;
    dev_info(&d->pdev->dev, "QPSOS reg_base=0x%06x\n", reg_base);

    cpr_write(d, 0x2f1094, 0);
    cpr_write(d, 0x2f1090, 0);
    cpr_write(d, reg_base + 4, 0);

    /* PLL setup */
    dev_info(&d->pdev->dev, "PLL setup: chip_ver=0x%08x\n", d->chip_ver);
    if (d->chip_ver == 0x10020)
        writel(0x00030130, d->bar1 + 0xC8);  /* PLL4 */
        else
            writel(0x00020236, d->bar1 + 0xC8);
    writel(0x00010239, d->bar1 + 0xCC);      /* PLL5 */

    writel(0, d->bar1 + 0x6CC);

    /* Step 13: Upload VIDEO FW @ 0x000000 */
    dev_info(&d->pdev->dev, "=== UPLOAD VIDEO FW @ 0x%08x ===\n", CARD_RAM_VIDEO_BASE);
    sz4 = ALIGN(fw_vid->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw_vid->size)
            memcpy(&word, fw_vid->data + i, min_t(u32, 4, fw_vid->size - i));
        word = le32_to_cpu(word);
        ret = cpr_write(d, CARD_RAM_VIDEO_BASE + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "VIDEO CPR write failed at 0x%x\n", i);
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
                memcpy(&host_val, fw_vid->data + j, min_t(size_t, 4, fw_vid->size - j));
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
                memcpy(&host_val, fw_aud->data + j, min_t(size_t, 4, fw_aud->size - j));
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

        dev_info(&d->pdev->dev, "FW verify: %d mismatches\n", mismatches);
    }

    dump_full_state(d, "AFTER-FW-UPLOAD");

    if (do_reset) {
        /* Step 14: 500us delay */
        udelay(500);

        /* Step 15: ResetArm(1) - start ARM */
        ret = dm_reset_arm(d, 1);
        if (ret)
            goto out;

        /* Step 16: 150ms delay */
        msleep(150);

        dump_full_state(d, "POST-ARM-BOOT");
    }

    dev_info(&d->pdev->dev, "FW download complete\n");

    out:
    release_firmware(fw_vid);
    release_firmware(fw_aud);
    return ret;
}

static int cqlcodec_reset(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "CQLCodec_Reset()\n");

    /* 1-2. Clear mailboxes */
    writel(0, d->bar1 + 0x6CC);
    writel(0, d->bar1 + 0x6C8);

    /* 3. Skip AllocEncodeTask(0) for now */

    /* 4. QPHCI_ReInit */
    ret = qphci_reinit(d);
    if (ret)
        return ret;

    /* 5. InitializeMemory - but DDR should already be up! */
    /* Skip on reload, only needed on fresh boot */

    /* 6-8. AO/VO/GPIO */
    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);
    writel(0, d->bar1 + REG_GPIO_DIR);
    writel(0, d->bar1 + REG_GPIO_VAL);

    /* 9. Delay */
    msleep(5);

    /* 10. Skip AllocEncodeTask(1) for now */

    return 0;
}

/* In cqlcodec.c or a new dm.c file: */

/**
 * dm_clear_interrupt - Clear HCI/DMA interrupts
 * @d: device structure
 * @int_mask: bitmask of interrupts to clear
 *            DM_INT_ARM_MSG  (0x01) - ARM message interrupt (bit 16)
 *            DM_INT_DMA_WRITE (0x02) - DMA write complete (bit 18)
 *            DM_INT_DMA_READ  (0x04) - DMA read complete (bit 17)
 *
 * Clears the specified interrupts by reading REG_HCI_INT_CTRL (0x800),
 * masking off bits 16-18, setting the requested clear bits, and writing back.
 *
 * Returns: 0 on success
 */
int dm_clear_interrupt(struct c985_poc *d, u32 int_mask)
{
    u32 hci_val;
    u32 clear_bits = 0;

    /* Map input mask to HCI register bits */
    if (int_mask & DM_INT_ARM_MSG)
        clear_bits |= BIT(16);
    if (int_mask & DM_INT_DMA_READ)
        clear_bits |= BIT(17);
    if (int_mask & DM_INT_DMA_WRITE)
        clear_bits |= BIT(18);

    if (!clear_bits)
        return 0;

    /* Read current value */
    hci_val = readl(d->bar1 + REG_HCI_INT_CTRL);

    /* Mask off bits 16-18, then set bits to clear */
    hci_val = (hci_val & 0xFFF8FFFF) | clear_bits;

    /* Write back to clear */
    writel(hci_val, d->bar1 + REG_HCI_INT_CTRL);

    return 0;
}

