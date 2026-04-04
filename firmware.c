// SPDX-License-Identifier: GPL-2.0
/*
 * firmware.c - ARM firmware loading for AVerMedia C985
 *
 * Based on CQLCodec_FWDownloadAll from Windows driver.
 *
 * Check idle state (if requested)
 * ResetArm(0) - Halt ARM
 * QPHCI_ReInit() - Reinitialize HCI
 * InitializeMemory() - Setup memory controller
 * AOSwitch / VOSwitch - Audio/Video output control
 * SetGPIODefaults() - GPIO configuration
 * Delay 50ms
 * Read reg 0x00, clear bit 13 - Audio DSP clock gate
 * Delay 1µs
 * FWDownload(audio, 0x100000) - Upload audio firmware
 * Delay 1ms
 * FWDownload(video, 0x000000) - Upload video firmware
 * Delay 500µs
 * ResetArm(1) - Start ARM
 * Dellay 150ms
 * Set volumes (AI=8, AO=8)
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

#include "qpfwencapi.h"
#include "qpfwapi.h"
#include "fw_debug.h"

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

#define DRIVER_VERSION "0.2.0-beta"


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


void c985_write_qpsos_config(struct c985_poc *d)
{
    u32 config_base;

    dev_info(&d->pdev->dev, "Writing QPSOS configuration...\n");

    /* Determine config base from QPSOS version */
    if (d->qpsos_version < 3) {
        config_base = 0x2F2000;  /* QPSOS2 */
    } else {
        config_base = 0x0F2000;  /* QPSOS3+ */
    }
    d->config_base = config_base;

    /* Write firmware mode configuration */
    cpr_write(d, 0x2F1094, d->fw_fixed_mode);
    cpr_write(d, 0x2F1090, d->fw_int_mode);
    cpr_write(d, config_base + 4, 0);

    dev_info(&d->pdev->dev, "  0x2F1094 = %u (FwFixedMode)\n", d->fw_fixed_mode);
    dev_info(&d->pdev->dev, "  0x2F1090 = %u (FwIntMode)\n", d->fw_int_mode);
    dev_info(&d->pdev->dev, "  0x%06X = 0 (config_base+4)\n", config_base + 4);

    /* PLL configuration for PCIe */
    if (d->bus_type == 1) {  /* QPHCI_BUS_PCI */
        u32 pll4_val, pll5_val;

        if (d->pll4_override != 0) {
            pll4_val = d->pll4_override;
        } else if (d->chip_ver == 0x10020) {
            pll4_val = 0x00030130;
        } else {
            pll4_val = 0x00020236;
        }

        if (d->pll5_override != 0) {
            pll5_val = d->pll5_override;
        } else {
            pll5_val = 0x00010239;
        }

        writel(pll4_val, d->bar1 + 0xC8);
        writel(pll5_val, d->bar1 + 0xCC);

        dev_info(&d->pdev->dev, "  PLL4 (0xC8) = 0x%08X\n", pll4_val);
        dev_info(&d->pdev->dev, "  PLL5 (0xCC) = 0x%08X\n", pll5_val);
    }

    /* Clear mailbox */
    writel(0, d->bar1 + 0x6CC);

    dev_info(&d->pdev->dev, "QPSOS configuration complete\n");
}

/* Module parameter for optional audio firmware loading */
static bool load_audio_fw = true;
module_param(load_audio_fw, bool, 0644);
MODULE_PARM_DESC(load_audio_fw, "Load audio firmware (default: true)");

int firmware_download_all(struct c985_poc *d)
{
    const struct firmware *fw_vid = NULL;
    const struct firmware *fw_aud = NULL;
    struct fw_metadata vid_meta = { .name = "Video" };
    struct fw_metadata aud_meta = { .name = "Audio" };

    u32 reg0, addr;
    int ret;

    dev_info(&d->pdev->dev,
             "========================================\n");
    dev_info(&d->pdev->dev,
             "=== FIRMWARE DOWNLOAD START ===\n");
    dev_info(&d->pdev->dev,
             "Driver Version: %s\n", DRIVER_VERSION);
    dev_info(&d->pdev->dev,
             "Upload Method: %s\n", use_dma ? "DMA" : "CPR");
    dev_info(&d->pdev->dev,
             "Audio Firmware: %s\n", load_audio_fw ? "Enabled" : "Disabled");
    dev_info(&d->pdev->dev,
             "========================================\n");

    /* ===== Load and validate firmware files ===== */
    dev_info(&d->pdev->dev, "Loading firmware files...\n");

    ret = request_firmware(&fw_vid, FW_VIDEO, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev,
                "Failed to load video firmware '%s': %d\n",
                FW_VIDEO, ret);
        goto out_cleanup;
    }

    /* Optionally load audio firmware */
    if (load_audio_fw) {
        ret = request_firmware(&fw_aud, FW_AUDIO, &d->pdev->dev);
        if (ret) {
            dev_err(&d->pdev->dev,
                    "Failed to load audio firmware '%s': %d\n",
                    FW_AUDIO, ret);
            goto out_free_vid;
        }

        ret = validate_firmware_header(d, fw_aud, "Audio");
        if (ret)
            goto out_free_all;
    } else {
        dev_info(&d->pdev->dev, "Skipping audio firmware (disabled)\n");
    }

    /* Validate headers */
    ret = validate_firmware_header(d, fw_vid, "Video");
    if (ret)
        goto out_free_all;

    /* Parse QPSOS version from video firmware */
    ret = parse_qpsos_header(d, fw_vid, &vid_meta.version);
    if (ret) {
        dev_warn(&d->pdev->dev,
                 "Could not parse QPSOS header - continuing anyway\n");
        /* Not fatal - use defaults */
    }

    /* Calculate checksums */
    vid_meta.size = fw_vid->size;
    vid_meta.crc32 = calculate_fw_crc32(fw_vid->data, fw_vid->size);

    if (load_audio_fw) {
        aud_meta.size = fw_aud->size;
        aud_meta.crc32 = calculate_fw_crc32(fw_aud->data, fw_aud->size);
    }

    /* Display firmware information */
    print_firmware_info(d, &vid_meta);
    if (load_audio_fw)
        print_firmware_info(d, &aud_meta);

    /* Check compatibility - skip if audio disabled */
    if (load_audio_fw) {
        ret = check_firmware_compatibility(d, &vid_meta, &aud_meta);
        if (ret)
            goto out_free_all;
    } else {
        dev_info(&d->pdev->dev,
                 "Skipping firmware compatibility check (audio disabled)\n");
    }

    /* ===== Initialize DMA if requested ===== */
    if (use_dma) {
        ret = c985_dma_init(d);
        if (ret) {
            dev_warn(&d->pdev->dev,
                     "DMA init failed (%d), falling back to CPR\n", ret);
            use_dma = 0;
        }
    }

    /* ===== Run DMA diagnostics if enabled ===== */
    if (use_dma && run_dma_tests) {
        dev_info(&d->pdev->dev,
                 "========================================\n");
        dev_info(&d->pdev->dev,
                 "=== DMA DIAGNOSTICS ===\n");
        dev_info(&d->pdev->dev,
                 "========================================\n");

        ret = c985_dma_test_vs_cpr(d);
        if (ret)
            dev_warn(&d->pdev->dev, "DMA vs CPR test failed\n");

        ret = c985_dma_test_loopback(d);
        if (ret) {
            dev_warn(&d->pdev->dev,
                     "DMA loopback failed, falling back to CPR\n");
            use_dma = 0;
        }

        dev_info(&d->pdev->dev,
                 "========================================\n");
    }

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
    dev_info(&d->pdev->dev, "Step 6: Clear bit 13 (audio DSP clock gate)\n");
    reg0 = readl(d->bar1 + 0x00);
    dev_info(&d->pdev->dev, "  Register 0x00: 0x%08x -> 0x%08x\n",
             reg0, reg0 & ~BIT(13));
    writel(reg0 & ~BIT(13), d->bar1 + 0x00);
    udelay(1);

    /* ===== STEP 7: Upload audio firmware (if enabled) ===== */
    if (load_audio_fw) {
        u32 audio_fw_aligned = ALIGN(fw_aud->size, 4);
        u32 zero_start = CARD_RAM_AUDIO_BASE + audio_fw_aligned;
        size_t zero_bytes = CARD_RAM_AUDIO_END - zero_start;

        /* ===== STEP 7a: Zero-fill audio region FIRST ===== */
        dev_info(&d->pdev->dev,
                 "Step 7a: Zero-fill %zu bytes (0x%06x to 0x%06x)\n",
                 zero_bytes, zero_start, CARD_RAM_AUDIO_END);

        if (use_dma) {
            /* DMA zero-fill (matches Windows driver) */
            u8 *zero_buf;
            size_t chunk_size = min_t(size_t, zero_bytes, 0x8000); /* 32KB max */

            zero_buf = kzalloc(chunk_size, GFP_KERNEL);
            if (zero_buf) {
                for (addr = zero_start; addr < CARD_RAM_AUDIO_END; ) {
                    size_t this_chunk = min_t(size_t, chunk_size,
                                              CARD_RAM_AUDIO_END - addr);
                    ret = c985_dma_write_sync(d, zero_buf, addr, this_chunk);
                    if (ret) {
                        dev_err(&d->pdev->dev, "Zero-fill DMA failed at 0x%x\n", addr);
                        kfree(zero_buf);
                        goto out_free_all;
                    }
                    addr += this_chunk;
                }
                kfree(zero_buf);
            } else {
                dev_warn(&d->pdev->dev, "Cannot alloc zero buffer, using CPR\n");
                goto zerofill_cpr;
            }
        } else {
            zerofill_cpr:
            for (addr = zero_start; addr < CARD_RAM_AUDIO_END; addr += 4) {
                cpr_write(d, addr, 0);

                if (((addr - zero_start) & 0xFFFF) == 0 && addr > zero_start) {
                    dev_info(&d->pdev->dev,
                             "  Zero-fill progress: 0x%x / 0x%x\n",
                             addr - zero_start,
                             CARD_RAM_AUDIO_END - zero_start);
                }
            }
        }

        /* ===== STEP 7b: Upload audio firmware ===== */
        dev_info(&d->pdev->dev,
                 "Step 7b: Upload audio firmware (%zu bytes to 0x%08x)\n",
                 fw_aud->size, CARD_RAM_AUDIO_BASE);

        ret = upload_firmware(d, fw_aud->data, fw_aud->size, CARD_RAM_AUDIO_BASE);
        if (ret) {
            dev_err(&d->pdev->dev, "Audio firmware upload failed: %d\n", ret);
            goto out_free_all;
        }

        /* Verify audio upload */
        ret = verify_firmware_in_card_memory(d, fw_aud, CARD_RAM_AUDIO_BASE, "Audio");
        if (ret) {
            dev_err(&d->pdev->dev, "Audio firmware verification failed\n");
            goto out_free_all;
        }
    }

    /* ===== STEP 8: Delay ===== */
    dev_info(&d->pdev->dev, "Step 8: 1ms delay\n");
    msleep(1);

    /* ===== QPSOS Configuration ===== */
    c985_write_qpsos_config(d);

    /* ===== STEP 9: Upload video firmware ===== */
    dev_info(&d->pdev->dev,
             "Step 9: Upload video firmware (%zu bytes to 0x%08x)\n",
             fw_vid->size, CARD_RAM_VIDEO_BASE);

    ret = upload_firmware(d, fw_vid->data, fw_vid->size, CARD_RAM_VIDEO_BASE);
    if (ret) {
        dev_err(&d->pdev->dev, "Video firmware upload failed: %d\n", ret);
        goto out_free_all;
    }

    /* Verify video upload */
    ret = verify_firmware_in_card_memory(d, fw_vid, CARD_RAM_VIDEO_BASE, "Video");
    if (ret) {
        dev_err(&d->pdev->dev, "Video firmware verification failed\n");
        goto out_free_all;
    }

    /* ===== STEP 10: Pre-boot delay ===== */
    dev_info(&d->pdev->dev, "Step 10: 250ms delay before ARM start\n");
    msleep(250);


    /* ===== STEP 11: Start ARM ===== */
    dev_info(&d->pdev->dev, "Step 11: Starting ARM core\n");
    ret = dm_reset_arm(d, 1);
    if (ret)
        goto out_free_all;

    wmb();
    udelay(10);  // Wait just 10 microseconds
    dev_info(&d->pdev->dev, "Immediate: 0x04=0x%08x\n", readl(d->bar1 + 0x04));
    udelay(100);
    dev_info(&d->pdev->dev, "100us: 0x04=0x%08x\n", readl(d->bar1 + 0x04));
    msleep(1);
    dev_info(&d->pdev->dev, "1ms: 0x04=0x%08x\n", readl(d->bar1 + 0x04));

    u32 val;
    cpr_read(d, 0x692E8, &val);
    dev_info(&d->pdev->dev, "Timer magic @ 0x692E8 = 0x%08x\n", val);
    cpr_read(d, 0x692EC, &val);
    dev_info(&d->pdev->dev, "Timer state @ 0x692EC = 0x%08x\n", val);
    cpr_read(d, 0x4C998, &val);
    dev_info(&d->pdev->dev, "Timer ticks @ 0x4C998 = 0x%08x\n", val);

    writel(0x01000000, d->bar1 + 0x0C);
    dev_info(&d->pdev->dev, "Reg 0x04=0x%08x - 0x00036c7d, 0x08=0x%08x - 0x00000000, 0x0C=0x%08x - 0x00000000 \n",
             readl(d->bar1 + 0x04),
             readl(d->bar1 + 0x08),
             readl(d->bar1 + 0x0C));

    //doorbell_test(d);

    u32 intc_base;
    cpr_read(d, 0x4C6C8, &intc_base);
    dev_info(&d->pdev->dev, "Interrupt controller base @ 0x4C6C8 = 0x%08x\n", intc_base);

    // Read the handler table at 0x5ECA0 + slot*8
    u32 slot2_handler, slot2_param;

    cpr_read(d, 0x5ECA0 + 2*8 + 0, &slot2_handler);  // Slot 2 handler
    cpr_read(d, 0x5ECA0 + 2*8 + 4, &slot2_param);    // Slot 2 param

    dev_info(&d->pdev->dev, "Slot 2 (IRQ 5): handler=0x%08x param=0x%08x\n",
             slot2_handler, slot2_param);

    // Also check 0x55468 mode config
    u32 mode_config;
    cpr_read(d, 0x55468, &mode_config);
    dev_info(&d->pdev->dev, "Mode config @ 0x55468 = 0x%08x (expect 0x02)\n",
             mode_config);

    msleep(500);

    u32 response = readl(d->bar1 + 0x6C8);
    dev_info(&d->pdev->dev, "After 500ms: 0x6C8=0x%08x\n", response);

    // The config byte
    u32 config_byte;
    cpr_read(d, 0x4C7E0, &config_byte);
    dev_info(&d->pdev->dev, "Config byte @ 0x4C7E0 = 0x%08x\n", config_byte);

    // The translation table
    u32 stride = 0x23; // 35
    u32 base_offset = (config_byte & 0xFF) * stride;

    for (int i = 0; i < 35; i++) {
        u32 hw_irq;
        cpr_read(d, 0x49CB4 + (base_offset + i) * 4, &hw_irq);
        if ((s32)hw_irq >= 0) {
            dev_dbg(&d->pdev->dev, "  LogIRQ[%2d] -> HwIRQ %d\n", i, hw_irq);
        }
    }


    /* ===== STEP 12: Initialize firmware communication ===== */
    dev_info(&d->pdev->dev, "Step 12: Initializing firmware communication\n");

    u32 int_enable;
    cpr_read(d, 0x04, &int_enable);  /* Interrupt controller enable */
    dev_info(&d->pdev->dev, "ARM interrupt enable: 0x%08x\n", int_enable);



    cpr_read(d, 0x04, &int_enable);  /* Interrupt controller enable */
    dev_info(&d->pdev->dev, "ARM interrupt enable: 0x%08x\n", int_enable);

    /* ===== Monitor firmware boot ===== */
    dev_info(&d->pdev->dev, "Monitoring firmware boot...\n");


    /* Enable PCIe interrupts */
    cpciectl_enable_interrupts(d);


    /* Set default volumes */
    d->ai_volume = 8;
    d->ao_volume = 8;

    dev_info(&d->pdev->dev,
             "========================================\n");
    dev_info(&d->pdev->dev,
             "=== FIRMWARE DOWNLOAD COMPLETE ===\n");
    dev_info(&d->pdev->dev,
             "QPSOS Version: %u\n", d->qpsos_version);
    dev_info(&d->pdev->dev,
             "Config Base: 0x%08x\n", d->config_base);
    dev_info(&d->pdev->dev,
             "Video FW CRC32: 0x%08x\n", vid_meta.crc32);
    if (load_audio_fw)
        dev_info(&d->pdev->dev,
                 "Audio FW CRC32: 0x%08x\n", aud_meta.crc32);
        else
            dev_info(&d->pdev->dev,
                     "Audio FW: DISABLED\n");
            dev_info(&d->pdev->dev,
                     "ARM Status: 0x80C=0x%08x 0x800=0x%08x\n",
                     readl(d->bar1 + 0x80C), readl(d->bar1 + 0x800));
            dev_info(&d->pdev->dev,
                     "========================================\n");

            ret = 0;

        out_free_all:
        if (load_audio_fw)
            release_firmware(fw_aud);
    out_free_vid:
    release_firmware(fw_vid);
    out_cleanup:
    if (use_dma)
        c985_dma_cleanup(d);
    return ret;
}
