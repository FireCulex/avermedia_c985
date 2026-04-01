// SPDX-License-Identifier: GPL-2.0
/*
 * firmware.c - ARM firmware loading for AVerMedia C985
 *
 * Based on CQLCodec_FWDownloadAll from Windows driver.
 */

#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "avermedia_c985.h"
#include "firmware.h"
#include "cpr.h"
#include "qphci.h"
#include "cqlcodec.h"
#include "interrupts.h"

/* Firmware files */
#define FW_VIDEO    "avermedia/qpvidfwpcie.bin"
#define FW_AUDIO    "avermedia/qpaudfw.bin"

/* Card RAM addresses */
#define CARD_RAM_VIDEO_BASE  0x00000000
#define CARD_RAM_AUDIO_BASE  0x00100000

/* QPSOS signature */
#define QPSOS_SIGNATURE      0x534f5351  /* "QSSO" little-endian */


/* Forward declarations */
static int fw_upload_to_card(struct c985_poc *d, const u8 *data,
                             size_t size, u32 card_addr);
static int fw_detect_qpsos_version(struct c985_poc *d,
                                   const struct firmware *fw_vid,
                                   u32 *version);
static void fw_setup_plls(struct c985_poc *d);
static int fw_verify_upload(struct c985_poc *d, const struct firmware *fw_vid,
                            const struct firmware *fw_aud);
static void fw_enable_hci_interrupts(struct c985_poc *d);

/**
 * firmware_download_all - Main firmware download sequence
 * @d: device context
 *
 * Implements CQLCodec_FWDownloadAll sequence:
 *   1. ResetArm(0) - halt ARM
 *   2. QPHCI_ReInit
 *   3. InitializeMemory
 *   4. AO/VO switch, GPIO defaults
 *   5. Delay 50ms
 *   6. Clear bit 13 of reg 0x00
 *   7. Upload audio FW to 0x100000
 *   8. Delay 1ms
 *   9. Upload video FW to 0x000000
 *  10. Delay 500us
 *  11. ResetArm(1) - start ARM
 *  12. Delay 150ms
 *
 * Returns 0 on success, negative error code on failure.
 */
int firmware_download_all(struct c985_poc *d)
{
    const struct firmware *fw_vid = NULL;
    const struct firmware *fw_aud = NULL;
    u32 qpsos_version = 0;
    u32 reg0;
    int ret;

    dev_info(&d->pdev->dev, "=== FIRMWARE DOWNLOAD START ===\n");

    /* Load firmware files */
    ret = request_firmware(&fw_vid, FW_VIDEO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load video FW '%s': %d\n", FW_VIDEO, ret);
        return ret;
    }
    dev_info(&d->pdev->dev, "Video FW: %zu bytes\n", fw_vid->size);

    ret = request_firmware(&fw_aud, FW_AUDIO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load audio FW '%s': %d\n", FW_AUDIO, ret);
        goto out_free_vid;
    }
    dev_info(&d->pdev->dev, "Audio FW: %zu bytes\n", fw_aud->size);

    /* Step 1: ResetArm(0) - halt ARM */
    ret = dm_reset_arm(d, 0);
    if (ret)
        goto out_free_all;

    /* Step 2: QPHCI_ReInit */
    ret = qphci_reinit(d);
    if (ret)
        goto out_free_all;

    /* Step 3: InitializeMemory */
    ret = codec_initialize_memory(d);
    if (ret)
        goto out_free_all;

    /* Step 4: AO/VO switch, GPIO defaults */
    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);
    gpio_set_defaults(d);

    /* Step 5: Delay 50ms */
    msleep(50);

    /* Step 6: Clear bit 13 of reg 0x00 */
    reg0 = readl(d->bar1 + 0x00);
    writel(reg0 & ~BIT(13), d->bar1 + 0x00);
    udelay(1);

    /* Step 7: Upload audio FW to 0x100000 */
    ret = fw_upload_to_card(d, fw_aud->data, fw_aud->size, CARD_RAM_AUDIO_BASE);
    if (ret) {
        dev_err(&d->pdev->dev, "Audio FW upload failed: %d\n", ret);
        goto out_free_all;
    }

    /* Step 8: Delay 1ms */

    /* Zero-fill from end of audio FW to 0x170624 */
    {
        u32 audio_end = ALIGN(fw_aud->size, 4);
        u32 fill_end = 0x70624;
        u32 addr;

        if (audio_end < fill_end) {
            dev_info(&d->pdev->dev, "Zero-filling 0x%06x to 0x%06x\n",
                     CARD_RAM_AUDIO_BASE + audio_end,
                     CARD_RAM_AUDIO_BASE + fill_end);
            for (addr = audio_end; addr < fill_end; addr += 4) {
                cpr_write(d, CARD_RAM_AUDIO_BASE + addr, 0);
            }
        }
    }
    msleep(1);

    /* Detect QPSOS version and setup registers */
    fw_detect_qpsos_version(d, fw_vid, &qpsos_version);
    fw_setup_plls(d);

    /* Clear mailbox before video FW upload */
    writel(0, d->bar1 + 0x6CC);

    /* Step 9: Upload video FW to 0x000000 */
    ret = fw_upload_to_card(d, fw_vid->data, fw_vid->size, CARD_RAM_VIDEO_BASE);
    if (ret) {
        dev_err(&d->pdev->dev, "Video FW upload failed: %d\n", ret);
        goto out_free_all;
    }

    /* After video FW upload, verify QPSOS regs survived */
    {
        u32 v1;
        cpr_read(d, 0x2f1094, &v1);
        dev_info(&d->pdev->dev, "Post-upload: 0x2f1094=0x%08x (expect 1)\n", v1);
    }

    /* Verify upload */
    ret = fw_verify_upload(d, fw_vid, fw_aud);
    if (ret)
        dev_warn(&d->pdev->dev, "FW verify found mismatches\n");

    /* Step 10: Delay 500us */
    udelay(500);

    /* Enable HCI interrupts BEFORE ARM boot */
    fw_enable_hci_interrupts(d);

    /* Step 11: ResetArm(1) - start ARM */
    ret = dm_reset_arm(d, 1);
    if (ret)
        goto out_free_all;

    /* Step 12: Delay 150ms for ARM to initialize */
    msleep(150);
    /* Test: can we write and read back 0x6C8? */
    writel(0xDEADBEEF, d->bar1 + 0x6C8);
    {
        u32 rb = readl(d->bar1 + 0x6C8);
        dev_info(&d->pdev->dev, "TEST: wrote 0xDEADBEEF to 0x6C8, read back 0x%08x\n", rb);
    }
    writel(0x00000000, d->bar1 + 0x6C8);  /* Clear it */
    /* Enable PCIe interrupts */
    cpciectl_enable_interrupts(d);

    /* Set default volumes */
    d->ai_volume = 8;
    d->ao_volume = 8;

    dev_info(&d->pdev->dev, "=== FIRMWARE DOWNLOAD COMPLETE ===\n");
    dev_info(&d->pdev->dev, "ARM status: 0x80C=0x%08x 0x800=0x%08x\n",
             readl(d->bar1 + 0x80C), readl(d->bar1 + 0x800));

    ret = 0;

    out_free_all:
    release_firmware(fw_aud);
    out_free_vid:
    release_firmware(fw_vid);
    return ret;
}

/**
 * fw_upload_to_card - Upload firmware data to card RAM via CPR
 */
static int fw_upload_to_card(struct c985_poc *d, const u8 *data,
                             size_t size, u32 card_addr)
{
    u32 sz4, i, word;
    int ret;

    dev_info(&d->pdev->dev, "Uploading %zu bytes to 0x%08x\n", size, card_addr);

    sz4 = ALIGN(size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < size)
            memcpy(&word, data + i, min_t(size_t, 4, size - i));
        word = le32_to_cpu(word);

        ret = cpr_write(d, card_addr + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "CPR write failed at offset 0x%x\n", i);
            return ret;
        }
    }

    return 0;
}

/**
 * fw_detect_qpsos_version - Detect QPSOS version from video firmware
 */
static int fw_detect_qpsos_version(struct c985_poc *d,
                                   const struct firmware *fw_vid,
                                   u32 *version)
{
    u32 sig, reg_base;
    u16 ver;

    *version = 0;

    if (fw_vid->size <= 0x108)
        return -EINVAL;

    memcpy(&sig, fw_vid->data + 0x100, 4);
    sig = le32_to_cpu(sig);

    if (sig != QPSOS_SIGNATURE) {
        dev_warn(&d->pdev->dev, "QPSOS signature not found (got 0x%08x)\n", sig);
        return -EINVAL;
    }

    memcpy(&ver, fw_vid->data + 0x106, 2);
    *version = le16_to_cpu(ver);

    dev_info(&d->pdev->dev, "QPSOS version: %u\n", *version);

    /* Setup register base based on version */
    reg_base = (*version < 3) ? 0x2f2000 : 0x0f2000;

    /* Initialize QPSOS control registers */
    cpr_write(d, 0x2f1094, 1);    /* FwFixedMode = 1 (from registry) */
    cpr_write(d, 0x2f1090, 0);    /* FwIntMode = 0 */
    cpr_write(d, reg_base + 4, 0);

    /* Verify writes */
    {
        u32 v1, v2, v3;
        cpr_read(d, 0x2f1094, &v1);
        cpr_read(d, 0x2f1090, &v2);
        cpr_read(d, reg_base + 4, &v3);
        dev_info(&d->pdev->dev, "QPSOS verify: 0x2f1094=0x%08x 0x2f1090=0x%08x reg+4=0x%08x\n",
                 v1, v2, v3);
    }


    return 0;
}

/**
 * fw_setup_plls - Configure PLL registers based on chip version
 */
static void fw_setup_plls(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "PLL setup: chip_ver=0x%08x\n", d->chip_ver);

    if (d->chip_ver == 0x10020)
        writel(0x00030130, d->bar1 + 0xC8);  /* PLL4 */
        else
            writel(0x00020236, d->bar1 + 0xC8);

    writel(0x00010239, d->bar1 + 0xCC);      /* PLL5 */
}

/**
 * fw_verify_upload - Verify firmware was uploaded correctly
 */
/**
 * fw_verify_upload - Verify firmware was uploaded correctly
 */
static int fw_verify_upload(struct c985_poc *d, const struct firmware *fw_vid,
                            const struct firmware *fw_aud)
{
    u32 card_val, host_val;
    int mismatches = 0;
    int i;

    dev_info(&d->pdev->dev, "=== FW VERIFY ===\n");

    /* Dump full ARM vector table */
    dev_info(&d->pdev->dev, "=== ARM VECTOR TABLE ===\n");
    for (i = 0; i < 64; i += 4) {
        cpr_read(d, i, &card_val);
        dev_info(&d->pdev->dev, "VEC[0x%02x]: 0x%08x\n", i, card_val);
    }

    /* Verify first 32 bytes of video FW */
    for (i = 0; i < 32; i += 4) {
        cpr_read(d, CARD_RAM_VIDEO_BASE + i, &card_val);
        host_val = 0;
        if (i < fw_vid->size)
            memcpy(&host_val, fw_vid->data + i, min_t(size_t, 4, fw_vid->size - i));
        host_val = le32_to_cpu(host_val);

        if (card_val != host_val) {
            dev_info(&d->pdev->dev, "[0x%04x]: card=0x%08x fw=0x%08x MISMATCH\n",
                     i, card_val, host_val);
            mismatches++;
        } else {
            dev_info(&d->pdev->dev, "[0x%04x]: card=0x%08x fw=0x%08x OK\n",
                     i, card_val, host_val);
        }
    }

    /* Verify first 16 bytes of audio FW */
    for (i = 0; i < 16; i += 4) {
        cpr_read(d, CARD_RAM_AUDIO_BASE + i, &card_val);
        host_val = 0;
        if (i < fw_aud->size)
            memcpy(&host_val, fw_aud->data + i, min_t(size_t, 4, fw_aud->size - i));
        host_val = le32_to_cpu(host_val);

        if (card_val != host_val) {
            dev_info(&d->pdev->dev, "AUD[0x%06x]: card=0x%08x fw=0x%08x MISMATCH\n",
                     CARD_RAM_AUDIO_BASE + i, card_val, host_val);
            mismatches++;
        } else {
            dev_info(&d->pdev->dev, "AUD[0x%06x]: card=0x%08x fw=0x%08x OK\n",
                     CARD_RAM_AUDIO_BASE + i, card_val, host_val);
        }
    }

    dev_info(&d->pdev->dev, "FW verify: %d mismatches\n", mismatches);

    return mismatches;
}

/**
 * fw_enable_hci_interrupts - Enable HCI interrupts for ARM communication
 *
 * From DM_EnableInterrupt:
 *   bits 0-2 = interrupt enable mask
 *   bits 16-18 = interrupt status (cleared by writing)
 */
static void fw_enable_hci_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar1 + 0x800);
    val |= 0x07;          /* Enable ARM msg, DMA write, DMA read */
    val &= 0xfff8ffff;    /* Clear status bits 16-18 */
    writel(val, d->bar1 + 0x800);

    dev_info(&d->pdev->dev, "HCI interrupts enabled: 0x800=0x%08x\n",
             readl(d->bar1 + 0x800));
}
