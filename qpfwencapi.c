// SPDX-License-Identifier: GPL-2.0
/*
 * qpfwencapi.c - Encoder API for ARM firmware
 *
 * The key insight from debugging:
 *   - reg 0x24 reads 0 after timeout = ARM consumed the interrupt
 *   - 0x6CC bit 0 still set = ARM didn't process the mailbox
 *   - 0x800 = 0 = HCI interrupts not enabled
 *
 * The ARM needs HCI interrupts enabled (0x800) BEFORE we can send
 * mailbox messages. The ARM uses HCI interrupt bit 16 to know when
 * to check the mailbox. Without this, the ARM sees the reg 0x24
 * interrupt but doesn't know what to do with it.
 *
 * Additionally, the Windows driver enables HCI during CQLCodec_RegisterISR
 * and the project init, BEFORE any mailbox communication.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "qpfwapi.h"
#include "qpfwencapi.h"
#include "nuc100.h"

/* -----------------------------------------------------------------------
 * HDMI video info
 * --------------------------------------------------------------------- */

struct hdmi_video_info {
    u16 hactive, vactive;
    u16 htotal, vtotal;
    u8  hpol, vpol;
    u8  scan_mode;
    u32 pixelclock;
    u16 rate;
    u32 qp_inctrl;
    u32 qp_insync;
    u32 qp_inres;
};

/* -----------------------------------------------------------------------
 * Compute InputControl
 * --------------------------------------------------------------------- */

static int compute_input_control(struct c985_poc *d,
                                 struct hdmi_video_info *info)
{
    u32 inctrl, insync;
    u16 rate;

    if (info->vpol == 0 && info->hpol == 1) {
        inctrl = 0x709; insync = 0x0f;
    } else if (info->vpol == 1 && info->hpol == 0) {
        inctrl = 0x689; insync = 0x12;
    } else if (info->vpol == 1 && info->hpol == 1) {
        inctrl = 0x609; insync = 0x11;
    } else {
        inctrl = 0x789; insync = 0x10;
    }

    if (info->scan_mode == 0) {
        inctrl &= 0xFFFFFFFE;
        info->vactive *= 2;
    }

    if (info->htotal && info->vtotal)
        rate = (u16)((info->pixelclock * 1000UL) /
        ((u32)info->htotal * (u32)info->vtotal));
    else
        rate = 60;

    if (rate >= 52 && rate <= 60)       rate = 60;
    else if (rate >= 41 && rate <= 50)  rate = 50;
    else if (rate >= 29 && rate <= 30)  rate = 30;
    else if (rate >= 23 && rate <= 25)  rate = 25;
    else if (rate > 60)                 rate = 60;

    inctrl |= ((u32)(rate | 0x40)) << 16;

    if ((info->hactive == 1280 && info->vactive == 720) ||
        (info->hactive == 720  && info->vactive == 576) ||
        (info->hactive == 720  && info->vactive == 480) ||
        (info->hactive == 1280 && info->vactive == 1024))
        inctrl |= 0x20000000;

    info->qp_inctrl = inctrl;
    info->qp_insync = insync;
    info->qp_inres = ((u32)info->vactive << 16) | info->hactive;
    info->rate = rate;

    dev_info(&d->pdev->dev,
             "HDMI: InCtrl=0x%08x InSync=0x%02x InRes=0x%08x rate=%u\n",
             inctrl, insync, info->qp_inres, rate);

    return 0;
}

/* -----------------------------------------------------------------------
 * SystemOpen
 * --------------------------------------------------------------------- */
// QPFWCODECAPI_SystemOpen
int qpfwencapi_system_open(struct c985_poc *d, u32 task_id, u32 function)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: SystemOpen task=%u func=0x%08x\n",
             task_id, function);

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    writel(function, d->bar1 + 0x6F8);

    message = (task_id << 16) | 0xF1;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * SystemLink
 * --------------------------------------------------------------------- */
// QPFWCODECAPI_SystemLink
int qpfwencapi_system_link(struct c985_poc *d, u32 task_id,
                           u32 vi, u32 vic, u32 vo, u32 voc,
                           u32 ai, u32 aic, u32 ao, u32 aoc)
{
    u32 link_val, message;
    int ret;

    dev_info(&d->pdev->dev,
             "ENC: SystemLink vi=%u/%u vo=%u/%u ai=%u/%u ao=%u/%u\n",
             vi, vic, vo, voc, ai, aic, ao, aoc);

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    link_val = (vi  & 0xf)
    | ((vic & 0xf) << 4)
    | ((vo  & 0xf) << 8)
    | ((voc & 0xf) << 12)
    | ((ai  & 0xf) << 16)
    | ((aic & 0xf) << 20)
    | ((ao  & 0xf) << 24)
    | ((aoc & 0xf) << 28);

    writel(link_val, d->bar1 + 0x6F8);

    message = (task_id << 16) | 0xF2;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * SetViuSyncCode
 * --------------------------------------------------------------------- */

int qpfwencapi_set_viu_sync_code(struct c985_poc *d, u32 task_id,
                                 u32 code1, u32 code2)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: SetViuSyncCode 0x%08x 0x%08x\n",
             code1, code2);

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    writel(2, d->bar1 + 0x6F8);
    writel(code1, d->bar1 + 0x6F4);
    writel(code2, d->bar1 + 0x6F0);

    message = (task_id << 16) | 0x10;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * UpdateConfig — no parameters, just cmd 6
 * --------------------------------------------------------------------- */

// QPFWENCAPI_UpdateConfig
int qpfwencapi_update_config(struct c985_poc *d, u32 task_id)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: UpdateConfig task=%u\n", task_id);

    ret = qpfwapi_mailbox_ready(d, 500);  /* ADD THIS */
    if (ret)
        return ret;

    message = (task_id << 16) | 6;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);  /* ADD THIS */
    return ret;
}

/* -----------------------------------------------------------------------
 * StartEncoder — no parameters, just cmd 1
 * --------------------------------------------------------------------- */

// QPFWENCAPI_StartEncoder

static int qpfwencapi_start_encoder(struct c985_poc *d, u32 task_id)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: StartEncoder task=%u\n", task_id);

    ret = qpfwapi_mailbox_ready(d, 500);  /* ADD THIS */
    if (ret)
        return ret;

    message = (task_id << 16) | 1;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);  /* ADD THIS */
    return ret;
}

/* -----------------------------------------------------------------------
 * StopEncoder
 * --------------------------------------------------------------------- */
// QPFWENCAPI_StopEncoder

int qpfwencapi_stop(struct c985_poc *d)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: StopEncoder\n");

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    writel(0, d->bar1 + 0x6F8);

    message = (0 << 16) | 2;
    ret = qpfwapi_send_message(d, 0, message);

    qpfwapi_mailbox_done(d);

    /* Wait for ARM to actually stop */
    msleep(200);

    /* Debug: check state after stop */
    dev_info(&d->pdev->dev, "ENC: post-stop 0x6CC=0x%08x 0x6C8=0x%08x 0x24=0x%08x\n",
             readl(d->bar1 + 0x6CC),
             readl(d->bar1 + 0x6C8),
             readl(d->bar1 + 0x24));

    return ret;
}

/* -----------------------------------------------------------------------
 * Write encoder config to register block
 * --------------------------------------------------------------------- */
// CQLCodec_LoadDefaultSettings

static void write_encoder_config(struct c985_poc *d, struct hdmi_video_info *vinfo)
{
    // Write encoder configuration using the correct register offsets
    writel(0x2101b219, d->bar1 + d->enc_reg_system_control);
    writel(((u32)d->height << 16) | d->width, d->bar1 + d->enc_reg_picture_resolution);
    writel(vinfo->qp_inctrl, d->bar1 + d->enc_reg_input_control);
    writel(0x520800f2, d->bar1 + d->enc_reg_audio_control_ex);
    writel(0x21121080, d->bar1 + d->enc_reg_audio_control_param);
    writel(0x00000010, d->bar1 + d->enc_reg_block_size);
    writel(((u32)d->height << 16) | d->width, d->bar1 + d->enc_reg_out_pic_resolution);
    writel(0xf199003c, d->bar1 + d->enc_reg_gop_loop_filter);
    writel(0x80002000, d->bar1 + d->enc_reg_filter_control);
    writel(0x1f4007d0, d->bar1 + d->enc_reg_bit_rate);
    writel(0x005003e8, d->bar1 + d->enc_reg_rate_control);

    dev_info(&d->pdev->dev, "ENC: config written to register block\n");
}

/* -----------------------------------------------------------------------
 * Enable HCI for ARM communication
 *
 * This MUST be done after ARM boot and before any mailbox commands.
 * The ARM firmware needs HCI interrupts enabled to process mailbox.
 *
 * From Windows driver: CQLCodec_RegisterISR enables interrupts,
 * and the project init happens after. But critically, the HCI
 * interrupt mask at 0x800 must have bit 16 set for the ARM to
 * receive host-to-ARM message notifications.
 *
 * The registers at 0x800+ are in the HCI block which the ARM
 * controls. After ARM boot, these may need specific initialization.
 * --------------------------------------------------------------------- */

static int enable_hci_communication(struct c985_poc *d)
{
    u32 val;

    /* Verify ARM is running */
    val = readl(d->bar1 + 0x80C);
    if (val != 1) {
        dev_err(&d->pdev->dev, "HCI: ARM not running (0x80C=0x%08x)\n", val);
        return -EIO;
    }

    /* Check mailbox is clear */
    val = readl(d->bar1 + 0x6CC);
    if (val & 1) {
        dev_warn(&d->pdev->dev, "HCI: mailbox busy, clearing\n");
        writel(0, d->bar1 + 0x6CC);
    }

    dev_info(&d->pdev->dev, "HCI: ARM communication ready\n");
    return 0;
}

/* -----------------------------------------------------------------------
 * Full start sequence
 * --------------------------------------------------------------------- */

int qpfwencapi_start(struct c985_poc *d)
{
    u32 task_id = 0;
    int ret;
    struct nuc100_hdmi_timing timing;
    struct hdmi_video_info vinfo = {0};
    int valid = 0;
    u8 scan_val;

    /* ---- 1. Detect HDMI ---- */
    ret = nuc100_get_hdmi_status(d);
    if (ret <= 0) {
        dev_err(&d->pdev->dev, "ENC: No HDMI signal\n");
        return -ENOLINK;
    }

    ret = nuc100_get_hdmi_timing(d, &timing, &valid);
    if (ret || !valid) {
        dev_err(&d->pdev->dev, "ENC: Cannot read HDMI timing\n");
        return -ENOLINK;
    }

    ret = nuc100_read_reg(d, 0x58, &scan_val);
    if (ret == 0)
        vinfo.scan_mode = (scan_val & 2) ? 0 : 1;
    else
        vinfo.scan_mode = 1;

    vinfo.hactive = timing.hactive;
    vinfo.vactive = timing.vactive;
    vinfo.htotal  = timing.htotal;
    vinfo.vtotal  = timing.vtotal;
    vinfo.hpol    = timing.hpol;
    vinfo.vpol    = timing.vpol;
    vinfo.pixelclock = timing.pixelclock;

    compute_input_control(d, &vinfo);

    d->width  = vinfo.hactive;
    d->height = vinfo.vactive;

    dev_info(&d->pdev->dev, "ENC: %ux%u @ %ufps InCtrl=0x%08x\n",
             d->width, d->height, vinfo.rate, vinfo.qp_inctrl);

    /* ---- 2. Enable HCI communication ---- */
    ret = enable_hci_communication(d);
    if (ret) {
        dev_err(&d->pdev->dev, "HCI enable failed: %d\n", ret);
        return ret;
    }

    /* ---- 3. SystemOpen ---- */
    ret = qpfwencapi_system_open(d, task_id, 0x80000011);
    if (ret) {
        dev_err(&d->pdev->dev, "SystemOpen failed: %d\n", ret);
        return ret;
    }

    /* ---- 4. SystemLink ---- */
    ret = qpfwencapi_system_link(d, task_id,
                                 8, 0, 1, 0,
                                 8, 0, 1, 0);
    if (ret) {
        dev_err(&d->pdev->dev, "SystemLink failed: %d\n", ret);
        return ret;
    }

    /* ---- 5. VIU sync codes ---- */
    ret = qpfwencapi_set_viu_sync_code(d, task_id,
                                       0xf1f1f1da, 0xb6f1f1b6);
    if (ret) {
        dev_err(&d->pdev->dev, "SetViuSyncCode failed: %d\n", ret);
        return ret;
    }

    /* ---- 6. Write encoder config ---- */
    write_encoder_config(d, &vinfo);

    /* ---- 7. UpdateConfig ---- */
    ret = qpfwencapi_update_config(d, task_id);
    if (ret) {
        dev_err(&d->pdev->dev, "UpdateConfig failed: %d\n", ret);
        return ret;
    }

    msleep(50);

    /* ---- 8. Start encoder ---- */
    ret = qpfwencapi_start_encoder(d, task_id);
    if (ret) {
        dev_err(&d->pdev->dev, "StartEncoder failed: %d\n", ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "ENC: start sequence complete\n");
    return 0;
}
