// SPDX-License-Identifier: GPL-2.0
/*
 * fw_debug.c - Firmware version detection and validation for AVerMedia C985
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "firmware.h"
#include "cpr.h"
#include "fw_debug.h"

/* QPSOS signature and offsets */
#define QPSOS_SIGNATURE         0x534F5351  /* "QSOS" */
#define QPSOS_SIG_OFFSET        0x100
#define QPSOS_VER_OFFSET        0x106

/**
 * calculate_fw_crc32 - Calculate CRC32 of firmware image
 */
u32 calculate_fw_crc32(const u8 *data, size_t size)
{
    u32 crc = 0xFFFFFFFF;
    size_t i;
    int j;

    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }

    return ~crc;
}

/**
 * parse_qpsos_header - Parse QPSOS version from video firmware
 */
int parse_qpsos_header(struct c985_poc *d,
                       const struct firmware *fw,
                       struct fw_version_info *info)
{
    u32 sig;
    u16 version;

    memset(info, 0, sizeof(*info));

    /* Validate firmware size */
    if (fw->size <= QPSOS_VER_OFFSET + 2) {
        dev_err(&d->pdev->dev,
                "Firmware too small: %zu bytes (need at least %d)\n",
                fw->size, QPSOS_VER_OFFSET + 2);
        return -EINVAL;
    }

    /* Read signature at offset 0x100 */
    memcpy(&sig, fw->data + QPSOS_SIG_OFFSET, 4);
    sig = le32_to_cpu(sig);

    /* Check magic "QSOS" */
    if (sig != QPSOS_SIGNATURE) {
        dev_warn(&d->pdev->dev,
                 "Invalid QPSOS signature: 0x%08x (expected 0x%08x)\n",
                 sig, QPSOS_SIGNATURE);
        return -EINVAL;
    }

    /* Read version at offset 0x106 (16-bit) */
    memcpy(&version, fw->data + QPSOS_VER_OFFSET, 2);
    version = le16_to_cpu(version);

    info->qpsos_signature = sig;
    info->qpsos_version = version;
    info->valid = true;

    /* Determine config base address */
    if (version < 3) {
        info->config_base = 0x2F2000;  /* QPSOS2 */
        dev_vdbg(&d->pdev->dev,
                 "QPSOS version %u (legacy layout, config @ 0x2F2000)\n",
                 version);
    } else {
        info->config_base = 0x0F2000;  /* QPSOS3+ */
        dev_vdbg(&d->pdev->dev,
                 "QPSOS version %u (optimized layout, config @ 0x0F2000)\n",
                 version);
    }

    /* Store in device struct */
    d->qpsos_version = version;
    d->config_base = info->config_base;

    return 0;
}

/**
 * validate_firmware_header - Check firmware magic numbers and structure
 */
int validate_firmware_header(struct c985_poc *d,
                             const struct firmware *fw,
                             const char *name)
{
    u32 arm_vector;

    dev_dbg(&d->pdev->dev, "Validating %s header\n", name);

    /* Check minimum size for ARM vectors */
    if (fw->size < 0x20) {
        dev_err(&d->pdev->dev,
                "%s: Too small (%zu bytes, need at least 32)\n",
                name, fw->size);
        return -EINVAL;
    }

    /* Check reset vector (should be a branch instruction or LDR PC) */
    memcpy(&arm_vector, fw->data, 4);
    arm_vector = le32_to_cpu(arm_vector);

    /* ARM branch: 0xEA000000 - 0xEAFFFFFF
     * ARM LDR PC: 0xE59FF000 - 0xE59FF0FF
     */
    if ((arm_vector & 0xFF000000) != 0xEA000000 &&
        (arm_vector & 0xFFFFF000) != 0xE59FF000) {
        dev_dbg(&d->pdev->dev,
                 "%s: Unexpected reset vector 0x%08x (may not be ARM code)\n",
                 name, arm_vector);
        /* Don't fail - could be valid but unusual */
        } else {
            dev_dbg(&d->pdev->dev,
                     "%s: Valid ARM reset vector 0x%08x\n",
                     name, arm_vector);
        }

        return 0;
}

/**
 * print_firmware_info - Display comprehensive firmware information
 */
void print_firmware_info(struct c985_poc *d,
                         struct fw_metadata *meta)
{
    if (meta->version.valid)
        dev_info(&d->pdev->dev,
                 "Firmware %s: %zu bytes, CRC 0x%08x, QPSOS v%u @ 0x%08x",
                 meta->name, meta->size, meta->crc32,
                 meta->version.qpsos_version,
                 meta->version.config_base);
        else
            dev_info(&d->pdev->dev,
                     "Firmware %s: %zu bytes, CRC 0x%08x",
                     meta->name, meta->size, meta->crc32);
}
/**
 * verify_firmware_in_card_memory - Read back and verify uploaded firmware
 */
int verify_firmware_in_card_memory(struct c985_poc *d,
                                   const struct firmware *fw,
                                   u32 card_addr,
                                   const char *name)
{
    //const size_t verify_size = min_t(size_t, fw->size, 1024);  /* Check first 1KB */
    const size_t verify_size = fw->size;
    u8 *readback;
    u32 card_val;
    int mismatches = 0;
    size_t i;
    int ret = 0;

    dev_dbg(&d->pdev->dev,
             "Verifying %s at 0x%08x (%zu bytes check)\n",
             name, card_addr, verify_size);

    /* Allocate readback buffer */
    readback = kmalloc(verify_size, GFP_KERNEL);
    if (!readback) {
        dev_err(&d->pdev->dev, "Cannot allocate verification buffer\n");
        return -ENOMEM;
    }

    /* Read back via CPR */
    for (i = 0; i < verify_size; i += 4) {
        ret = cpr_read(d, card_addr + i, &card_val);
        if (ret) {
            dev_err(&d->pdev->dev,
                    "CPR read failed at 0x%08x\n",
                    card_addr + (u32)i);
            goto out_free;
        }

        /* Store as little-endian */
        card_val = cpu_to_le32(card_val);
        memcpy(readback + i, &card_val, min_t(size_t, 4, verify_size - i));
    }

    /* Compare */
    for (i = 0; i < verify_size; i++) {
        if (readback[i] != fw->data[i]) {
            if (mismatches < 10) {  /* Limit spam */
                dev_err(&d->pdev->dev,
                        "  Offset 0x%04zx: card=0x%02x fw=0x%02x MISMATCH\n",
                        i, readback[i], fw->data[i]);
            }
            mismatches++;
        }
    }

    if (mismatches == 0) {
        dev_vdbg(&d->pdev->dev,
                 "  Verification PASSED (%zu bytes)\n", verify_size);
        ret = 0;
    } else {
        dev_err(&d->pdev->dev,
                "  Verification FAILED: %d/%zu bytes mismatched\n",
                mismatches, verify_size);
        ret = -EIO;
    }

    out_free:
    kfree(readback);
    return ret;
}

/**
 * check_firmware_compatibility - Verify firmware versions are compatible
 *
 * Note: Results are stored in the fw_metadata structures, not in c985_poc.
 * Caller should use vid_meta->version.config_base etc.
 */
int check_firmware_compatibility(struct c985_poc *d,
                                 struct fw_metadata *vid_meta,
                                 struct fw_metadata *aud_meta)
{
    u32 qpsos_version;
    u32 config_base;

    if (vid_meta->version.valid) {
        qpsos_version = vid_meta->version.qpsos_version;
        config_base = vid_meta->version.config_base;
    } else {
        dev_warn(&d->pdev->dev,
                 "Video firmware has no QPSOS header - using defaults\n");
        qpsos_version = 2;  /* Assume QPSOS2 */
        config_base = 0x2F2000;

        /* Store defaults in metadata for caller */
        vid_meta->version.qpsos_version = qpsos_version;
        vid_meta->version.config_base = config_base;
    }

    /* Check for known problematic combinations */
    if (qpsos_version >= 3 && aud_meta->size < 0x60000) {
        dev_warn(&d->pdev->dev,
                 "Warning: QPSOS3+ with small audio firmware may have issues\n");
    }

    dev_vdbg(&d->pdev->dev,
             "Firmware compatibility: QPSOS %u with %zu byte audio FW\n",
             qpsos_version, aud_meta->size);

    return 0;
}
