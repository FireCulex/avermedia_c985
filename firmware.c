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
#include "dma.h"

/* Firmware files */
#define FW_VIDEO    "avermedia/qpvidfwpcie.bin"
#define FW_AUDIO    "avermedia/qpaudfw.bin"

/* Card RAM addresses */
#define CARD_RAM_VIDEO_BASE     0x00000000
#define CARD_RAM_AUDIO_BASE     0x00100000
#define CARD_RAM_AUDIO_END      0x00170624  /* Zero-fill target */

/* QPSOS signature and offsets */
#define QPSOS_SIGNATURE         0x534F5351  /* "QSSO" */
#define QPSOS_SIG_OFFSET        0x100
#define QPSOS_VER_OFFSET        0x106

/* Module parameter: upload method */
static int use_dma = 1;
module_param(use_dma, int, 0644);
MODULE_PARM_DESC(use_dma, "Use DMA for firmware upload (1=DMA, 0=CPR)");

/* Module parameter: run tests */
static int run_dma_tests = 0;
module_param(run_dma_tests, int, 0644);
MODULE_PARM_DESC(run_dma_tests, "Run DMA diagnostics before firmware upload");

/* ========== Upload Methods ========== */

/**
 * upload_via_cpr - Upload firmware using register-based CPR
 */
static int upload_via_cpr(struct c985_poc *d, const u8 *data,
                          size_t size, u32 card_addr)
{
    size_t aligned_size = ALIGN(size, 4);
    u32 word;
    size_t i;
    int ret;

    dev_info(&d->pdev->dev, "CPR upload: %zu bytes to 0x%08x\n", size, card_addr);

    for (i = 0; i < aligned_size; i += 4) {
        word = 0;
        if (i < size)
            memcpy(&word, data + i, min_t(size_t, 4, size - i));
        word = le32_to_cpu(word);

        ret = cpr_write(d, card_addr + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "CPR write failed at 0x%08x\n",
                    card_addr + (u32)i);
            return ret;
        }

        /* Progress every 64KB */
        if ((i & 0xFFFF) == 0 && i > 0)
            dev_info(&d->pdev->dev, "  CPR progress: %zu / %zu\n", i, size);
    }

    dev_info(&d->pdev->dev, "CPR upload complete\n");
    return 0;
}

/**
 * upload_via_dma - Upload firmware using DMA
 */
static int upload_via_dma(struct c985_poc *d, const u8 *data,
                          size_t size, u32 card_addr)
{
    int ret;

    dev_info(&d->pdev->dev, "DMA upload: %zu bytes to 0x%08x\n", size, card_addr);

    ret = c985_dma_write_sync(d, data, card_addr, size);
    if (ret) {
        dev_err(&d->pdev->dev, "DMA upload failed: %d\n", ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "DMA upload complete\n");
    return 0;
}

/**
 * upload_firmware - Upload firmware using configured method
 */
static int upload_firmware(struct c985_poc *d, const u8 *data,
                           size_t size, u32 card_addr)
{
    if (use_dma)
        return upload_via_dma(d, data, size, card_addr);
    else
        return upload_via_cpr(d, data, size, card_addr);
}

/* ========== Verification ========== */

/**
 * verify_upload - Verify firmware was uploaded correctly
 */
static int verify_upload(struct c985_poc *d, const char *name,
                         const u8 *fw_data, size_t fw_size, u32 card_addr)
{
    u32 card_val, fw_val;
    int mismatches = 0;
    size_t check_size = min(fw_size, (size_t)64);  /* Check first 64 bytes */
    size_t i;

    dev_info(&d->pdev->dev, "Verifying %s at 0x%08x (%zu bytes)\n",
             name, card_addr, fw_size);

    for (i = 0; i < check_size; i += 4) {
        cpr_read(d, card_addr + i, &card_val);

        fw_val = 0;
        if (i < fw_size)
            memcpy(&fw_val, fw_data + i, min_t(size_t, 4, fw_size - i));
        fw_val = le32_to_cpu(fw_val);

        if (card_val != fw_val) {
            dev_warn(&d->pdev->dev,
                     "  [0x%04x]: card=0x%08x expected=0x%08x MISMATCH\n",
                     (u32)(card_addr + i), card_val, fw_val);
            mismatches++;
        }
    }

    if (mismatches == 0) {
        dev_info(&d->pdev->dev, "  Verification PASSED\n");
        return 0;
    } else {
        dev_err(&d->pdev->dev, "  Verification FAILED: %d mismatches\n",
                mismatches);
        return -EIO;
    }
}

/**
 * dump_arm_vectors - Dump ARM exception vector table
 */
static void dump_arm_vectors(struct c985_poc *d)
{
    u32 val;
    int i;

    dev_info(&d->pdev->dev, "=== ARM Vector Table ===\n");
    for (i = 0; i < 0x40; i += 4) {
        cpr_read(d, i, &val);
        dev_info(&d->pdev->dev, "  [0x%02x] = 0x%08x\n", i, val);
    }
}

/* ========== QPSOS Detection ========== */

/**
 * detect_qpsos_version - Detect QPSOS version from video firmware
 */
static int detect_qpsos_version(struct c985_poc *d,
                                const struct firmware *fw_vid,
                                u32 *version)
{
    u32 sig;
    u16 ver;
    u32 reg_base;

    *version = 0;

    if (fw_vid->size <= QPSOS_VER_OFFSET + 2) {
        dev_warn(&d->pdev->dev, "Video FW too small for QPSOS header\n");
        return -EINVAL;
    }

    memcpy(&sig, fw_vid->data + QPSOS_SIG_OFFSET, 4);
    sig = le32_to_cpu(sig);

    if (sig != QPSOS_SIGNATURE) {
        dev_warn(&d->pdev->dev, "QPSOS signature not found: 0x%08x\n", sig);
        return -EINVAL;
    }

    memcpy(&ver, fw_vid->data + QPSOS_VER_OFFSET, 2);
    *version = le16_to_cpu(ver);

    dev_info(&d->pdev->dev, "QPSOS version: %u\n", *version);

    /* Initialize QPSOS control registers */
    reg_base = (*version < 3) ? 0x2F2000 : 0x0F2000;

    cpr_write(d, 0x2F1094, 1);      /* FwFixedMode = 1 */
    cpr_write(d, 0x2F1090, 0);      /* FwIntMode = 0 */
    cpr_write(d, reg_base + 4, 0);

    /* Verify */
    {
        u32 v1, v2, v3;
        cpr_read(d, 0x2F1094, &v1);
        cpr_read(d, 0x2F1090, &v2);
        cpr_read(d, reg_base + 4, &v3);
        dev_info(&d->pdev->dev, "QPSOS regs: 0x2F1094=0x%x 0x2F1090=0x%x base+4=0x%x\n",
                 v1, v2, v3);
    }

    return 0;
}

/**
 * setup_plls - Configure PLL registers
 */
static void setup_plls(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "PLL setup: chip_ver=0x%08x\n", d->chip_ver);

    if (d->chip_ver == 0x10020)
        writel(0x00030130, d->bar1 + 0xC8);  /* PLL4 */
        else
            writel(0x00020236, d->bar1 + 0xC8);

    writel(0x00010239, d->bar1 + 0xCC);      /* PLL5 */
}

/**
 * enable_hci_interrupts - Enable ARM communication interrupts
 */
static void enable_hci_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar1 + 0x800);
    val |= 0x07;            /* Enable bits 0-2 */
    val &= 0xFFF8FFFF;      /* Clear status bits 16-18 */
    writel(val, d->bar1 + 0x800);

    dev_info(&d->pdev->dev, "HCI interrupts: 0x800=0x%08x\n",
             readl(d->bar1 + 0x800));
}

/* ========== Main Download Sequence ========== */

/**
 * firmware_download_all - Complete firmware download sequence
 */
int firmware_download_all(struct c985_poc *d)
{
    const struct firmware *fw_vid = NULL;
    const struct firmware *fw_aud = NULL;
    u32 qpsos_version = 0;
    u32 reg0;
    u32 addr;
    int ret;

    dev_info(&d->pdev->dev, "========================================\n");
    dev_info(&d->pdev->dev, "=== FIRMWARE DOWNLOAD START ===\n");
    dev_info(&d->pdev->dev, "Method: %s\n", use_dma ? "DMA" : "CPR");
    dev_info(&d->pdev->dev, "========================================\n");

    /* Initialize DMA if requested */
    if (use_dma) {
        ret = c985_dma_init(d);
        if (ret) {
            dev_warn(&d->pdev->dev,
                     "DMA init failed (%d), falling back to CPR\n", ret);
            use_dma = 0;
        }
    }

    /* Run DMA diagnostics if enabled */
    if (use_dma && run_dma_tests) {
        dev_info(&d->pdev->dev, "========================================\n");
        dev_info(&d->pdev->dev, "=== DMA DIAGNOSTICS ===\n");
        dev_info(&d->pdev->dev, "========================================\n");

        /* Test DMA vs CPR address space */
        ret = c985_dma_test_vs_cpr(d);
        if (ret)
            dev_warn(&d->pdev->dev, "DMA vs CPR test failed\n");

        /* Test DMA loopback */
        ret = c985_dma_test_loopback(d);
        if (ret) {
            dev_warn(&d->pdev->dev,
                     "DMA loopback failed, falling back to CPR\n");
            use_dma = 0;
        }

        dev_info(&d->pdev->dev, "========================================\n");
    }

    /* Load firmware files */
    ret = request_firmware(&fw_vid, FW_VIDEO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load '%s': %d\n", FW_VIDEO, ret);
        goto out_cleanup;
    }
    dev_info(&d->pdev->dev, "Video FW: %zu bytes\n", fw_vid->size);

    ret = request_firmware(&fw_aud, FW_AUDIO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load '%s': %d\n", FW_AUDIO, ret);
        goto out_free_vid;
    }
    dev_info(&d->pdev->dev, "Audio FW: %zu bytes\n", fw_aud->size);

    /* ===== STEP 1: Halt ARM ===== */
    dev_info(&d->pdev->dev, "Step 1: Halt ARM\n");
    ret = dm_reset_arm(d, 0);
    if (ret)
        goto out_free_all;

    /* ===== STEP 2: Reinitialize HCI ===== */
    dev_info(&d->pdev->dev, "Step 2: Reinit HCI\n");
    ret = qphci_reinit(d);
    if (ret)
        goto out_free_all;

    /* ===== STEP 3: Initialize memory controller ===== */
    dev_info(&d->pdev->dev, "Step 3: Init memory\n");
    ret = codec_initialize_memory(d);
    if (ret)
        goto out_free_all;

    /* ===== STEP 4: AO/VO switch, GPIO ===== */
    dev_info(&d->pdev->dev, "Step 4: AO/VO/GPIO setup\n");
    cqlcodec_ao_switch(d, !d->ao_enable);
    cqlcodec_vo_switch(d, !d->vo_enable);
    gpio_set_defaults(d);

    /* ===== STEP 5: Delay ===== */
    dev_info(&d->pdev->dev, "Step 5: 50ms delay\n");
    msleep(50);

    /* ===== STEP 6: Clear bit 13 of reg 0x00 ===== */
    dev_info(&d->pdev->dev, "Step 6: Clear bit 13\n");
    reg0 = readl(d->bar1 + 0x00);
    writel(reg0 & ~BIT(13), d->bar1 + 0x00);
    udelay(1);

    /* ===== STEP 7: Upload audio firmware ===== */
    dev_info(&d->pdev->dev, "Step 7: Upload audio FW\n");
    ret = upload_firmware(d, fw_aud->data, fw_aud->size, CARD_RAM_AUDIO_BASE);
    if (ret) {
        dev_err(&d->pdev->dev, "Audio FW upload failed\n");
        goto out_free_all;
    }

    /* Verify audio upload */
    verify_upload(d, "Audio FW", fw_aud->data, fw_aud->size, CARD_RAM_AUDIO_BASE);

    /* ===== STEP 7b: Zero-fill to 0x170624 ===== */
    {
        u32 audio_end = CARD_RAM_AUDIO_BASE + ALIGN(fw_aud->size, 4);
        dev_info(&d->pdev->dev, "Step 7b: Zero-fill 0x%06x to 0x%06x\n",
                 audio_end, CARD_RAM_AUDIO_END);
        for (addr = audio_end; addr < CARD_RAM_AUDIO_END; addr += 4)
            cpr_write(d, addr, 0);
    }

    /* ===== STEP 8: Delay ===== */
    dev_info(&d->pdev->dev, "Step 8: 1ms delay\n");
    msleep(1);

    /* ===== QPSOS setup ===== */
    dev_info(&d->pdev->dev, "QPSOS detection\n");
    detect_qpsos_version(d, fw_vid, &qpsos_version);
    setup_plls(d);

    /* Clear mailbox */
    writel(0, d->bar1 + 0x6CC);

    /* ===== STEP 9: Upload video firmware ===== */
    dev_info(&d->pdev->dev, "Step 9: Upload video FW\n");
    ret = upload_firmware(d, fw_vid->data, fw_vid->size, CARD_RAM_VIDEO_BASE);
    if (ret) {
        dev_err(&d->pdev->dev, "Video FW upload failed\n");
        goto out_free_all;
    }

    /* Verify video upload */
    verify_upload(d, "Video FW", fw_vid->data, fw_vid->size, CARD_RAM_VIDEO_BASE);

    /* Dump ARM vectors for debugging */
    dump_arm_vectors(d);

    /* ===== STEP 10: Delay ===== */
    dev_info(&d->pdev->dev, "Step 10: 500us delay\n");
    udelay(500);

    /* ===== Enable HCI interrupts ===== */
    dev_info(&d->pdev->dev, "Enable HCI interrupts\n");
    enable_hci_interrupts(d);

    /* ===== STEP 11: Start ARM ===== */
    dev_info(&d->pdev->dev, "Step 11: Start ARM\n");
    ret = dm_reset_arm(d, 1);
    if (ret)
        goto out_free_all;

    /* ===== STEP 12: Wait for ARM init ===== */
    dev_info(&d->pdev->dev, "Step 12: 150ms delay for ARM init\n");
    msleep(150);

    /* Test mailbox access */
    writel(0xDEADBEEF, d->bar1 + 0x6C8);
    {
        u32 rb = readl(d->bar1 + 0x6C8);
        dev_info(&d->pdev->dev, "Mailbox test: wrote 0xDEADBEEF, read 0x%08x\n", rb);
    }
    writel(0, d->bar1 + 0x6C8);

    /* Enable PCIe interrupts */
    cpciectl_enable_interrupts(d);

    /* Set default volumes */
    d->ai_volume = 8;
    d->ao_volume = 8;

    dev_info(&d->pdev->dev, "========================================\n");
    dev_info(&d->pdev->dev, "=== FIRMWARE DOWNLOAD COMPLETE ===\n");
    dev_info(&d->pdev->dev, "ARM status: 0x80C=0x%08x 0x800=0x%08x\n",
             readl(d->bar1 + 0x80C), readl(d->bar1 + 0x800));
    dev_info(&d->pdev->dev, "========================================\n");

    ret = 0;

    out_free_all:
    release_firmware(fw_aud);
    out_free_vid:
    release_firmware(fw_vid);
    out_cleanup:
    if (use_dma)
        c985_dma_cleanup(d);
    return ret;
}
