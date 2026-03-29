// SPDX-License-Identifier: GPL-2.0
// qpfwencapi.c — ARM firmware encoder API for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

#include "avermedia_c985.h"
#include "qpfwapi.h"
#include "qpfwencapi.h"
#include "nuc100.h"

/* -----------------------------------------------------------------------
 * HDMI video info for dynamic InputControl computation
 * --------------------------------------------------------------------- */


struct qp_buffer_descriptor {
    u32 phys_addr_lo;
    u32 phys_addr_hi;
    u32 size;
    u32 status; /* 0=free, 1=ready, 2=done */
    u32 next;   /* linked list pointer */
    u32 flags;  /* buffer type, etc. */
};

struct hdmi_video_info {
    u16 hactive;
    u16 vactive;
    u16 htotal;
    u16 vtotal;
    u8  hpol;       /* 1 = positive */
    u8  vpol;       /* 1 = positive */
    u8  scan_mode;  /* 1 = progressive, 0 = interlace */
    u32 pixelclock; /* in kHz */

    /* Computed */
    u16 rate;
    u32 qp_inctrl;
    u32 qp_insync;
    u32 qp_inres;
};

/* -----------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------- */

static int compute_input_control(struct c985_poc *d, struct hdmi_video_info *info);
static int qpfwencapi_submit_buffer(struct c985_poc *d, u32 buffer_id, dma_addr_t phys_addr, u32 size);

/* -----------------------------------------------------------------------
 * Register write helper
 * --------------------------------------------------------------------- */

static int enc_reg_write(struct c985_poc *d, u32 reg, u32 val)
{
    dev_dbg(&d->pdev->dev, "ENC: reg 0x%04x = 0x%08x\n", reg, val);
    writel(val, d->bar1 + reg);
    return 0;
}

/* -----------------------------------------------------------------------
 * Encoder register setters
 * --------------------------------------------------------------------- */

static int qpfwencapi_set_system_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_system_control, val);
}

static int qpfwencapi_set_picture_resolution(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_picture_resolution, val);
}

static int qpfwencapi_set_input_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_input_control, val);
}

static int qpfwencapi_set_rate_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_rate_control, val);
}

static int qpfwencapi_set_bit_rate(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_bit_rate, val);
}

static int qpfwencapi_set_filter_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_filter_control, val);
}

static int qpfwencapi_set_gop_loop_filter(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_gop_loop_filter, val);
}

static int qpfwencapi_set_out_picture_resolution(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_out_pic_resolution, val);
}

static int qpfwencapi_set_et_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_et_control, val);
}

static int qpfwencapi_set_block_size(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_block_size, val);
}

static int qpfwencapi_set_audio_control(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_audio_control_param, val);
}

static int qpfwencapi_set_audio_control_ex(struct c985_poc *d, u32 val)
{
    return enc_reg_write(d, d->enc_reg_audio_control_ex, val);
}

/* -----------------------------------------------------------------------
 * Compute InputControl from detected HDMI timing
 * --------------------------------------------------------------------- */

static int compute_input_control(struct c985_poc *d, struct hdmi_video_info *info)
{
    u32 inctrl;
    u32 insync;
    u16 rate;

    /* Step 1: Base values from polarity */
    if (info->vpol == 0 && info->hpol == 1) {
        /* V-, H+ */
        inctrl = 0x709;
        insync = 0x0f;
    } else if (info->vpol == 1 && info->hpol == 0) {
        /* V+, H- */
        inctrl = 0x689;
        insync = 0x12;
    } else if (info->vpol == 1 && info->hpol == 1) {
        /* V+, H+ (most common for 1080p) */
        inctrl = 0x609;
        insync = 0x11;
    } else {
        /* V-, H- */
        inctrl = 0x789;
        insync = 0x10;
    }

    /* Step 2: Interlace adjustment */
    if (info->scan_mode == 0) {
        inctrl &= 0xFFFFFFFE;
        info->vactive = info->vactive * 2;
    }

    /* Step 3: Calculate and quantize frame rate */
    if (info->htotal && info->vtotal) {
        rate = (u16)((info->pixelclock * 1000UL) /
        ((u32)info->htotal * (u32)info->vtotal));
    } else {
        rate = 60;
    }

    dev_info(&d->pdev->dev, "HDMI: raw frame rate = %u\n", rate);

    /* Quantize (from decompile) */
    if (rate < 61 && rate > 51) {
        rate = 60;
    } else if (rate < 51 && rate > 40) {
        rate = 50;
    } else if (rate < 31 && rate > 28) {
        rate = 30;
    } else if (rate < 26 && rate > 22) {
        rate = 25;
    } else if (rate > 60) {
        rate = 60;
    }

    dev_info(&d->pdev->dev, "HDMI: quantized frame rate = %u\n", rate);

    /* Step 4: Merge rate into InCtrl */
    inctrl |= ((u32)(rate | 0x40)) << 16;

    /* Step 5: Resolution-specific flag */
    if ((info->hactive == 1280 && info->vactive == 720) ||
        (info->hactive == 720  && info->vactive == 576) ||
        (info->hactive == 720  && info->vactive == 480) ||
        (info->hactive == 1280 && info->vactive == 1024)) {
        inctrl |= 0x20000000;
        }

        /* Step 6: QP_InRes = (vactive << 16) | hactive */
        info->qp_inctrl = inctrl;
    info->qp_insync = insync;
    info->qp_inres = ((u32)info->vactive << 16) | info->hactive;
    info->rate = rate;

    dev_info(&d->pdev->dev,
             "HDMI: computed QP_InCtrl=0x%08x QP_InSync=0x%02x QP_InRes=0x%08x\n",
             inctrl, insync, info->qp_inres);

    return 0;
}

/* -----------------------------------------------------------------------
 * SystemLink: connect video/audio inputs to outputs
 * --------------------------------------------------------------------- */

int qpfwencapi_system_link(struct c985_poc *d, u32 task_id,
                           u32 vid_in, u32 vid_in_ch,
                           u32 vid_out, u32 vid_out_ch,
                           u32 aud_in, u32 aud_in_ch,
                           u32 aud_out, u32 aud_out_ch)
{
    u32 link_val;
    u32 message;
    int ret;

    dev_dbg(&d->pdev->dev, "ENC: SystemLink task=%u vin=%u vout=%u ain=%u aout=%u\n",
            task_id, vid_in, vid_out, aud_in, aud_out);

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    link_val = (vid_in & 0xf)
    | ((vid_in_ch & 0xf) << 4)
    | ((vid_out & 0xf) << 8)
    | ((vid_out_ch & 0xf) << 12)
    | ((aud_in & 0xf) << 16)
    | ((aud_in_ch & 0xf) << 20)
    | ((aud_out & 0xf) << 24)
    | ((aud_out_ch & 0xf) << 28);

    enc_reg_write(d, 0x6F8, link_val);

    message = (task_id << 16) | 0xF2;
    ret = qpfwapi_send_message(d, task_id, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * SystemOpen
 * --------------------------------------------------------------------- */

int qpfwencapi_system_open(struct c985_poc *d, u32 task_id, u32 function)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: SystemOpen task=%u function=0x%08x\n",
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
 * VIU Sync Codes
 * --------------------------------------------------------------------- */

int qpfwencapi_set_viu_sync_code(struct c985_poc *d, u32 task_id,
                                 u32 code1, u32 code2)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: SetViuSyncCode 0x%08x 0x%08x\n", code1, code2);

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
 * UpdateConfig — tell ARM to apply settings
 * --------------------------------------------------------------------- */

int qpfwencapi_update_config(struct c985_poc *d, u32 task_id)
{
    u32 message;

    dev_info(&d->pdev->dev, "ENC: UpdateConfig task=%u\n", task_id);

    message = (task_id << 16) | 6;
    return qpfwapi_send_message(d, task_id, message);
}

/* -----------------------------------------------------------------------
 * Submit DMA buffer for encoded frame output
 * --------------------------------------------------------------------- */

static int qpfwencapi_submit_buffer(struct c985_poc *d, u32 buffer_id,
                                    dma_addr_t phys_addr, u32 size)
{
    u32 message;
    int ret;

    dev_info(&d->pdev->dev, "ENC: Submit buffer id=%u addr=0x%llx size=%u\n",
             buffer_id, (unsigned long long)phys_addr, size);

    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret)
        return ret;

    /* Write buffer info to registers */
    writel(lower_32_bits(phys_addr), d->bar1 + 0x6F4);  /* Buffer address */
    writel(size, d->bar1 + 0x6F0);                      /* Buffer size */
    writel(buffer_id, d->bar1 + 0x6F8);                 /* Buffer ID */

    /* Command 0x40 = "Add video buffer" */
    message = (0 << 16) | 0x40;
    ret = qpfwapi_send_message(d, 0, message);

    qpfwapi_mailbox_done(d);
    return ret;
}

/* -----------------------------------------------------------------------
 * Start the encoder
 * --------------------------------------------------------------------- */

static int qpfwencapi_start_encoder(struct c985_poc *d, u32 task_id)
{
    u32 message;

    dev_info(&d->pdev->dev, "ENC: start encoder task=%u\n", task_id);

    message = (task_id << 16) | 1;
    return qpfwapi_send_message(d, task_id, message);
}

/* -----------------------------------------------------------------------
 * Full encoder start sequence
 * --------------------------------------------------------------------- */

int qpfwencapi_start(struct c985_poc *d)
{
    u32 task_id = 0;
    int ret;
    struct nuc100_hdmi_timing timing;
    struct hdmi_video_info vinfo;
    int valid = 0;
    u8 scan_val;

    /* ---- Read actual HDMI signal parameters ---- */
    ret = nuc100_get_hdmi_status(d);
    if (ret <= 0) {
        dev_err(&d->pdev->dev, "ENC: No HDMI signal detected!\n");
        return -ENOLINK;
    }

    ret = nuc100_get_hdmi_timing(d, &timing, &valid);
    if (ret || !valid) {
        dev_err(&d->pdev->dev, "ENC: Cannot read HDMI timing\n");
        return -ENOLINK;
    }

    dev_info(&d->pdev->dev,
             "HDMI: %ux%u htotal=%u vtotal=%u pclk=%u hpol=%u vpol=%u\n",
             timing.hactive, timing.vactive,
             timing.htotal, timing.vtotal,
             timing.pixelclock,
             timing.hpol, timing.vpol);

    /* Read scan mode from NUC100 register 0x58 */
    memset(&vinfo, 0, sizeof(vinfo));
    ret = nuc100_read_reg(d, 0x58, &scan_val);
    if (ret == 0) {
        vinfo.scan_mode = (scan_val & 2) ? 0 : 1;
        dev_info(&d->pdev->dev, "HDMI: scan=0x%02x (%s)\n",
                 scan_val, vinfo.scan_mode ? "progressive" : "interlace");
    } else {
        vinfo.scan_mode = 1; /* assume progressive */
        dev_info(&d->pdev->dev, "HDMI: scan reg read failed, assuming progressive\n");
    }

    vinfo.hactive = timing.hactive;
    vinfo.vactive = timing.vactive;
    vinfo.htotal = timing.htotal;
    vinfo.vtotal = timing.vtotal;
    vinfo.hpol = timing.hpol;
    vinfo.vpol = timing.vpol;
    vinfo.pixelclock = timing.pixelclock;

    compute_input_control(d, &vinfo);

    /* Update device state */
    d->width = vinfo.hactive;
    d->height = vinfo.vactive;

    /* ---- Enable ARM-to-host interrupt ---- */
    {
        u32 reg4 = readl(d->bar1 + 0x04);
        writel(reg4 | 0x1000000, d->bar1 + 0x04);
        dev_info(&d->pdev->dev, "ENC: enabled ARM interrupt (reg4: 0x%08x -> 0x%08x)\n",
                 reg4, readl(d->bar1 + 0x04));
    }

    dev_info(&d->pdev->dev, "ENC: starting capture %ux%u @ %ufps\n",
             d->width, d->height, vinfo.rate);

    /* Enable all interrupts */
    writel(7, d->bar1 + 0x800);
    writel(readl(d->bar1 + 0x4000) | 1, d->bar1 + 0x4000);

    /* Clear pending */
    writel(0x40000000, d->bar1 + 0x4030);

    /* 1. SystemOpen */
    ret = qpfwencapi_system_open(d, task_id, 0x80000011);
    if (ret) return ret;

    /* 2. SystemLink */
    ret = qpfwencapi_system_link(d, task_id,
                                 8, 0, 1, 0,
                                 8, 0, 1, 0);
    if (ret) return ret;

    /* 3. Mailbox ready */
    ret = qpfwapi_mailbox_ready(d, 500);
    if (ret) return ret;

    /* 4. VIU Sync Codes */
    ret = qpfwencapi_set_viu_sync_code(d, task_id, 0xf1f1f1da, 0xb6f1f1b6);
    if (ret) return ret;

    /* 5. Encoder config with COMPUTED values */
    ret = qpfwencapi_set_system_control(d, 0x2101b219);
    if (ret) return ret;

    ret = qpfwencapi_set_picture_resolution(d,
                                            ((u32)d->height << 16) | d->width);
    if (ret) return ret;

    /* USE COMPUTED InputControl */
    ret = qpfwencapi_set_input_control(d, vinfo.qp_inctrl);
    if (ret) return ret;

    dev_info(&d->pdev->dev, "ENC: InputControl = 0x%08x (was hardcoded 0x0f7c0609)\n",
             vinfo.qp_inctrl);

    ret = qpfwencapi_set_rate_control(d, 0x005003e8);
    if (ret) return ret;

    ret = qpfwencapi_set_bit_rate(d, 0x1f4007d0);
    if (ret) return ret;

    ret = qpfwencapi_set_filter_control(d, 0x80002000);
    if (ret) return ret;

    ret = qpfwencapi_set_gop_loop_filter(d, 0xf199003c);
    if (ret) return ret;

    ret = qpfwencapi_set_out_picture_resolution(d,
                                                ((u32)d->height << 16) | d->width);
    if (ret) return ret;

    ret = qpfwencapi_set_et_control(d, 0x00000000);
    if (ret) return ret;

    ret = qpfwencapi_set_block_size(d, 0x00000010);
    if (ret) return ret;

    ret = qpfwencapi_set_audio_control(d, 0x21121080);
    if (ret) return ret;

    ret = qpfwencapi_set_audio_control_ex(d, 0x520800f2);
    if (ret) return ret;

    /* 6. UpdateConfig — tell ARM to apply settings */
    ret = qpfwencapi_update_config(d, task_id);
    if (ret) return ret;

    /* ---- Submit test buffer ---- */
    {
        dma_addr_t test_phys;
        void *test_buf;

        test_buf = dma_alloc_coherent(&d->pdev->dev, 512*1024,
                                      &test_phys, GFP_KERNEL);
        if (test_buf) {
            ret = qpfwencapi_submit_buffer(d, 0, test_phys, 512*1024);
            if (ret) {
                dma_free_coherent(&d->pdev->dev, 512*1024, test_buf, test_phys);
                dev_err(&d->pdev->dev, "Failed to submit test buffer\n");
                return ret;
            }
            dev_info(&d->pdev->dev, "ENC: Test buffer submitted\n");
            /* Keep buffer allocated for now */
        } else {
            dev_warn(&d->pdev->dev, "Failed to allocate test buffer\n");
        }
    }

    /* 7. Start encoder */
    ret = qpfwencapi_start_encoder(d, task_id);
    if (ret) return ret;

    qpfwapi_mailbox_done(d);

    /* ---- POST-START DIAGNOSTIC ---- */
    {
        int i;

        dev_info(&d->pdev->dev, "=== POST-START DIAGNOSTIC ===\n");
        for (i = 0; i < 16; i++) {
            u32 val = readl(d->bar1 + 0x6C0 + (i * 4));
            dev_info(&d->pdev->dev, "BAR1[0x%03x] = 0x%08x\n",
                     0x6C0 + (i * 4), val);
        }

        dev_info(&d->pdev->dev, "HCI[0x800] = 0x%08x\n", readl(d->bar1 + 0x800));
        dev_info(&d->pdev->dev, "HCI[0x804] = 0x%08x\n", readl(d->bar1 + 0x804));
        dev_info(&d->pdev->dev, "HCI[0x808] = 0x%08x\n", readl(d->bar1 + 0x808));
        dev_info(&d->pdev->dev, "HCI[0x80C] = 0x%08x\n", readl(d->bar1 + 0x80C));

        for (i = 0; i < 8; i++) {
            u32 val = readl(d->bar1 + 0x810 + (i * 4));
            dev_info(&d->pdev->dev, "DMA[0x%03x] = 0x%08x\n",
                     0x810 + (i * 4), val);
        }

        /* Poll briefly to see if ARM sends anything */
        for (i = 0; i < 20; i++) {
            u32 pci_st = readl(d->bar1 + 0x4030);
            u32 hci_st = readl(d->bar1 + 0x800);
            u32 msg = readl(d->bar1 + 0x6C8);
            u32 mbox = readl(d->bar1 + 0x6CC);

            if ((pci_st & 0x40000000) || (hci_st & 0x70000) || msg || (mbox & 1)) {
                dev_info(&d->pdev->dev,
                         "POLL[%d]: pci=0x%08x hci=0x%08x msg=0x%08x mbox=0x%08x\n",
                         i, pci_st, hci_st, msg, mbox);
            }
            msleep(100);
        }
    }

    dev_info(&d->pdev->dev, "ENC: capture started\n");
    return 0;
}

/* -----------------------------------------------------------------------
 * Stop encoder
 * --------------------------------------------------------------------- */

int qpfwencapi_stop(struct c985_poc *d)
{
    u32 task_id = 0;
    u32 message;

    dev_info(&d->pdev->dev, "ENC: stopping capture\n");

    message = (task_id << 16) | 2;
    return qpfwapi_send_message(d, task_id, message);
}
