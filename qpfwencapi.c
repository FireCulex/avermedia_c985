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

int qpfwencapi_update_config(struct c985_poc *d, u32 task_id)
{
    u32 message;

    dev_info(&d->pdev->dev, "ENC: UpdateConfig task=%u\n", task_id);

    message = (task_id << 16) | 6;
    return qpfwapi_send_message(d, task_id, message);
}

/* -----------------------------------------------------------------------
 * StartEncoder — no parameters, just cmd 1
 * --------------------------------------------------------------------- */

static int qpfwencapi_start_encoder(struct c985_poc *d, u32 task_id)
{
    u32 message;

    dev_info(&d->pdev->dev, "ENC: StartEncoder task=%u\n", task_id);

    message = (task_id << 16) | 1;
    return qpfwapi_send_message(d, task_id, message);
}

/* -----------------------------------------------------------------------
 * StopEncoder
 * --------------------------------------------------------------------- */

int qpfwencapi_stop(struct c985_poc *d)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: StopEncoder\n");

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    writel(0, d->bar1 + 0x6F8);  /* bStopAtGOP = 0 */

    message = (0 << 16) | 2;
    ret = qpfwapi_send_message(d, 0, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * Write encoder config to register block
 * --------------------------------------------------------------------- */

static void write_encoder_config(struct c985_poc *d,
                                 struct hdmi_video_info *vinfo)
{
    writel(0x2101b219, d->bar1 + 0x6C4);
    writel(((u32)d->height << 16) | d->width, d->bar1 + 0x6C8);
    writel(vinfo->qp_inctrl, d->bar1 + 0x6CC);
    writel(0x520800f2, d->bar1 + 0x6D0);
    writel(0x21121080, d->bar1 + 0x6D4);
    writel(0x00000010, d->bar1 + 0x6D8);
    writel(((u32)d->height << 16) | d->width, d->bar1 + 0x6DC);
    writel(0xf199003c, d->bar1 + 0x6E0);
    writel(0x80002000, d->bar1 + 0x6E4);
    writel(0x1f4007d0, d->bar1 + 0x6E8);
    writel(0x005003e8, d->bar1 + 0x6EC);
    writel(0x00000000, d->bar1 + 0x6F0);

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
    int i;

    dev_info(&d->pdev->dev, "HCI: enabling ARM communication\n");

    /* Verify ARM is running */
    val = readl(d->bar1 + 0x80C);
    if (val != 1) {
        dev_err(&d->pdev->dev, "HCI: ARM not running (0x80C=0x%08x)\n", val);
        return -EIO;
    }

    /* Try different interrupt enable mechanisms */

    /* Method 1: Write to 0x800 (HCI interrupt mask) */
    dev_info(&d->pdev->dev, "HCI: trying method 1 (0x800)\n");
    writel(0x70000, d->bar1 + 0x800);
    writel(0x70000, d->bar1 + 0x804);  /* Clear pending */
    msleep(10);

    /* Method 2: Try 0x808 (might be ARM interrupt enable) */
    dev_info(&d->pdev->dev, "HCI: trying method 2 (0x808)\n");
    val = readl(d->bar1 + 0x808);
    dev_info(&d->pdev->dev, "HCI: 0x808 before = 0x%08x\n", val);
    writel(val | 0x70000, d->bar1 + 0x808);
    val = readl(d->bar1 + 0x808);
    dev_info(&d->pdev->dev, "HCI: 0x808 after = 0x%08x\n", val);

    /* Method 3: Try writing directly to ARM's interrupt controller base */
    dev_info(&d->pdev->dev, "HCI: trying method 3 (CPR write to ARM NVIC)\n");
    /* ARM Cortex-M NVIC is at 0xE000E100, but we need to translate this
     * to the card's address space. The firmware loads at specific addresses.
     * Try writing to what might be the ARM's interrupt enable register.
     */

    /* Method 4: Enable PCI-level interrupt routing */
    dev_info(&d->pdev->dev, "HCI: enabling PCI interrupts\n");
    val = readl(d->bar1 + 0x04);
    writel(val | 0x1000000, d->bar1 + 0x04);

    val = readl(d->bar1 + 0x4000);
    writel(val | 0x1, d->bar1 + 0x4000);

    /* Clear all pending */
    writel(0x70000, d->bar1 + 0x804);
    writel(0x40010000, d->bar1 + 0x4030);

    /* Method 5: Try the "streaming port" register that showed up */
    dev_info(&d->pdev->dev, "HCI: 0x840 = 0x%08x\n", readl(d->bar1 + 0x840));

    /* Method 6: Write to 0x80C to kick ARM? */
    dev_info(&d->pdev->dev, "HCI: trying method 6 (kick ARM via 0x80C)\n");
    writel(1, d->bar1 + 0x80C);

    /* Wait and check */
    msleep(100);

    dev_info(&d->pdev->dev, "HCI: final state check\n");
    dev_info(&d->pdev->dev, "  0x800 = 0x%08x\n", readl(d->bar1 + 0x800));
    dev_info(&d->pdev->dev, "  0x804 = 0x%08x\n", readl(d->bar1 + 0x804));
    dev_info(&d->pdev->dev, "  0x808 = 0x%08x\n", readl(d->bar1 + 0x808));
    dev_info(&d->pdev->dev, "  0x80C = 0x%08x\n", readl(d->bar1 + 0x80C));
    dev_info(&d->pdev->dev, "  0x810 = 0x%08x\n", readl(d->bar1 + 0x810));

    /* Try sending a test interrupt */
    dev_info(&d->pdev->dev, "HCI: sending test interrupt\n");
    writel(0x2000000, d->bar1 + 0x24);
    msleep(50);

    val = readl(d->bar1 + 0x24);
    dev_info(&d->pdev->dev, "HCI: 0x24 after interrupt = 0x%08x\n", val);

    if (val & 0x2000000) {
        dev_warn(&d->pdev->dev, "ARM did not clear test interrupt!\n");
    } else {
        dev_info(&d->pdev->dev, "ARM cleared test interrupt - good sign!\n");
    }

    /* Check if ARM sent anything back */
    dev_info(&d->pdev->dev, "  0x6C8 = 0x%08x\n", readl(d->bar1 + 0x6C8));
    dev_info(&d->pdev->dev, "  0x6CC = 0x%08x\n", readl(d->bar1 + 0x6CC));

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
