// SPDX-License-Identifier: GPL-2.0
// v4l2.c — V4L2 video capture interface for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include "include/abi/qp_buffer_descriptor.h"
#include "include/abi/_kspin.h"
#include "include/abi/_qp_ksstream_header.h"
#include "include/abi/_ksstream_pointer.h"
#include "include/abi/_guid.h"


/* VIDEO_MAX_FRAME may not be defined in all kernel versions */
#ifndef VIDEO_MAX_FRAME
#define VIDEO_MAX_FRAME 32
#endif

#include "pins.h"
#include "structs.h"
#include "v4l2.h"

#define SYNC_PRINT(fmt, ...) do { \
printk(KERN_EMERG "C985_HALT: " fmt "\n", ##__VA_ARGS__); \
mdelay(100); \
} while (0)


#define C985_V4L2_NAME "c985-video"

/* -----------------------------------------------------------------------
 * Videobuf2 operations
 * --------------------------------------------------------------------- */

static int c985_queue_setup(struct vb2_queue *vq,
                            unsigned int *nbuffers, unsigned int *nplanes,
                            unsigned int sizes[], struct device *alloc_devs[])
{
    unsigned int size = 1920 * 1080 * 3 / 2;  /* ~3MB for raw YUV */

    SYNC_PRINT("ENTER queue_setup vq=%px", vq);


    if (*nplanes)
        return sizes[0] < size ? -EINVAL : 0;

    *nplanes = 1;
    sizes[0] = size;
    *nbuffers = min(*nbuffers, 8u);
    SYNC_PRINT("EXIT queue_setup");

    return 0;
}

static int c985_buf_prepare(struct vb2_buffer *vb)
{
    unsigned int size = 1920 * 1080 * 3 / 2;

    if (vb2_plane_size(vb, 0) < size)
        return -EINVAL;

    return 0;
}

static void c985_stop_streaming(struct vb2_queue *vq)
{
    struct c985_poc *d = vb2_get_drv_priv(vq);
    struct c985_buffer *buf, *tmp;
    unsigned long flags;

    dev_info(&d->pdev->dev, "stop_streaming\n");

    /* Return ALL buffers tracked by videobuf2 */
    spin_lock_irqsave(&d->buf_lock, flags);

    /* First, clean up our internal tracking */
    list_for_each_entry_safe(buf, tmp, &d->buf_list, list) {
        if (buf->buf_desc) {
            kfree(buf->header);
            kfree(buf->queue_entry);
            kfree(buf->buf_desc);
            buf->buf_desc = NULL;
            buf->header = NULL;
            buf->queue_entry = NULL;
        }
        list_del(&buf->list);
    }
    spin_unlock_irqrestore(&d->buf_lock, flags);

    /* Now return ALL buffers videobuf2 knows about */
    {
        struct vb2_buffer *vb;
        unsigned long idx;

        for (idx = 0; idx < VIDEO_MAX_FRAME; idx++) {
            vb = vb2_get_buffer(&d->vb2_queue, idx);
            if (!vb)
                continue;

            if (vb->state == VB2_BUF_STATE_ACTIVE ||
                vb->state == VB2_BUF_STATE_QUEUED) {
                vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
                }
        }
    }

    d->sequence = 0;

    dev_info(&d->pdev->dev, "stop_streaming complete\n");
}

static void c985_buf_queue(struct vb2_buffer *vb)
{
    SYNC_PRINT("ENTER buf_queue vb=%px", vb);

    struct c985_poc *d = vb2_get_drv_priv(vb->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct c985_buffer *buf = container_of(vbuf, struct c985_buffer, vb);
    unsigned long flags;

    pr_info("C985_BQ: enter vb=%px d=%px\n", vb, d);

    if (!d) {
        pr_err("C985_BQ: d is NULL, bailing\n");
        return;
    }

    pr_info("C985_BQ: buf=%px buf->queue_entry=%px\n", buf, buf->queue_entry);
    pr_info("C985_BQ: buf_list=%px buf_lock=%px\n", &d->buf_list, &d->buf_lock);

    pr_info("C985_BQ: about to spin_lock\n");
    spin_lock_irqsave(&d->buf_lock, flags);
    pr_info("C985_BQ: spin_lock acquired\n");

    if (!buf->queue_entry) {
        struct _QP_BUFFER_DESCRIPTOR *desc;
        struct _QP_KSSTREAM_HEADER *header;
        struct QUEUE_ENTRY *entry;

        pr_info("C985_BQ: allocating desc/header/entry\n");

        desc = kzalloc(sizeof(struct _QP_BUFFER_DESCRIPTOR), GFP_ATOMIC);
        pr_info("C985_BQ: desc=%px\n", desc);

        header = kzalloc(sizeof(struct _QP_KSSTREAM_HEADER), GFP_ATOMIC);
        pr_info("C985_BQ: header=%px\n", header);

        entry = kzalloc(sizeof(struct QUEUE_ENTRY), GFP_ATOMIC);
        pr_info("C985_BQ: entry=%px\n", entry);

        if (!desc || !header || !entry) {
            pr_err("C985_BQ: alloc failed desc=%px header=%px entry=%px\n",
                   desc, header, entry);
            kfree(desc);
            kfree(header);
            kfree(entry);
            spin_unlock_irqrestore(&d->buf_lock, flags);
            return;
        }

        desc->DataBufferArray = header;
        desc->ulBufferSize = vb2_plane_size(vb, 0);
        desc->ulBufferOffset = 0;
        desc->ulTotalUsed = 0;
        desc->ulFlags = 0;

        pr_info("C985_BQ: desc->ulBufferSize=%u\n", desc->ulBufferSize);

        header->Data = vb2_plane_vaddr(vb, 0);
        header->FrameExtent = desc->ulBufferSize;
        header->DataUsed = 0;

        pr_info("C985_BQ: header->Data=%px\n", header->Data);

        buf->buf_desc = desc;
        buf->header = header;
        buf->queue_entry = entry;

        entry->Data = desc;
        entry->pNext = NULL;

        pr_info("C985_BQ: entry/desc/header wired up\n");
    } else {
        pr_info("C985_BQ: buf already has queue_entry, skipping alloc\n");
    }

    pr_info("C985_BQ: about to list_add_tail\n");
    list_add_tail(&buf->list, &d->buf_list);
    pr_info("C985_BQ: list_add_tail done\n");

    spin_unlock_irqrestore(&d->buf_lock, flags);
    pr_info("C985_BQ: exit ok\n");

    SYNC_PRINT("EXIT buf_queue");


}

/* -----------------------------------------------------------------------
 * V4L2 ioctl operations
 * --------------------------------------------------------------------- */

static int c985_querycap(struct file *file, void *priv,
                         struct v4l2_capability *cap)
{
    struct c985_poc *d = video_drvdata(file);

    strscpy(cap->driver, C985_V4L2_NAME, sizeof(cap->driver));
    strscpy(cap->card, "AVerMedia Live Gamer HD (C985)", sizeof(cap->card));
    snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s", pci_name(d->pdev));

    return 0;
}

static int c985_enum_input(struct file *file, void *priv,
                           struct v4l2_input *inp)
{
    if (inp->index > 0)
        return -EINVAL;

    inp->type = V4L2_INPUT_TYPE_CAMERA;
    strscpy(inp->name, "HDMI", sizeof(inp->name));
    inp->capabilities = 0;

    return 0;
}

static int c985_g_input(struct file *file, void *priv, unsigned int *i)
{
    *i = 0;
    return 0;
}

static int c985_s_input(struct file *file, void *priv, unsigned int i)
{
    return (i == 0) ? 0 : -EINVAL;
}

static int c985_enum_fmt(struct file *file, void *priv,
                         struct v4l2_fmtdesc *f)
{
    if (f->index > 0)
        return -EINVAL;

    f->pixelformat = V4L2_PIX_FMT_H264;
    f->flags = V4L2_FMT_FLAG_COMPRESSED;

    return 0;
}

static int c985_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
    struct c985_poc *d = video_drvdata(file);

    f->fmt.pix.width = d->width;
    f->fmt.pix.height = d->height;
    f->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    f->fmt.pix.field = V4L2_FIELD_NONE;
    f->fmt.pix.bytesperline = 0;  /* compressed */
    f->fmt.pix.sizeimage = 512 * 1024;  /* max frame size */
    f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

    return 0;
}

static int c985_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
    struct c985_poc *d = video_drvdata(file);

    d->width = 1920;
    d->height = 1080;

    return c985_g_fmt(file, priv, f);
}

static int c985_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
    f->fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    f->fmt.pix.field = V4L2_FIELD_NONE;
    f->fmt.pix.bytesperline = 0;
    f->fmt.pix.sizeimage = 512 * 1024;
    f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    f->fmt.pix.flags = V4L2_FMT_FLAG_COMPRESSED;

    return 0;
}

static int c985_g_parm(struct file *file, void *priv,
                       struct v4l2_streamparm *parm)
{
    if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
    parm->parm.capture.timeperframe.numerator = 1;
    parm->parm.capture.timeperframe.denominator = 60;
    parm->parm.capture.readbuffers = 4;

    return 0;
}

static int c985_s_parm(struct file *file, void *priv,
                       struct v4l2_streamparm *parm)
{
    if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    return c985_g_parm(file, priv, parm);
}

/* -----------------------------------------------------------------------
 * V4L2 file operations
 * --------------------------------------------------------------------- */

static const struct v4l2_file_operations c985_fops = {
    .owner          = THIS_MODULE,
    .open           = v4l2_fh_open,
    .release        = vb2_fop_release,
    .read           = NULL,
    .poll           = vb2_fop_poll,
    .mmap           = vb2_fop_mmap,
    .unlocked_ioctl = video_ioctl2,
};

void c985_v4l2_unregister(struct c985_poc *d)
{
    if (!d) {
        pr_warn("c985_v4l2_unregister: NULL device pointer\n");
        return;
    }

    if (!d->v4l2_registered) {
        dev_dbg(&d->pdev->dev, "V4L2 was never registered, skipping unregister\n");
        return;
    }

    dev_info(&d->pdev->dev, "unregistering V4L2 device\n");

    /* 1. Unregister video device (removes /dev/videoX and sysfs entry) */
    if (video_is_registered(&d->vdev)) {
        video_unregister_device(&d->vdev);
    }

    /* 2. Release videobuf2 queue */
    vb2_queue_release(&d->vb2_queue);

    /* 3. Unregister V4L2 device */
    v4l2_device_unregister(&d->v4l2_dev);

    /* 4. Free kfifo */
    kfifo_free(&d->frame_fifo);

    /* 5. Destroy mutex */
    mutex_destroy(&d->v4l2_lock);

    d->v4l2_registered = false;
    dev_info(&d->pdev->dev, "V4L2 device unregistered\n");
}

/* -----------------------------------------------------------------------
 * Helper Functions - Streaming Setup
 * --------------------------------------------------------------------- */

static void c985_init_streaming_state(struct c985_poc *d)
{
    d->sequence = 0;
    d->encoder_running = false;
    d->active_task_id = (u32)-1;
}

static void c985_cleanup_on_error(struct c985_poc *d)
{
    struct c985_buffer *buf, *tmp;
    unsigned long flags;

    d->active_task_id = (u32)-1;
    d->encoder_running = false;

    spin_lock_irqsave(&d->buf_lock, flags);
    list_for_each_entry_safe(buf, tmp, &d->buf_list, list) {
        if (buf->buf_desc) {
            kfree(buf->header);
            kfree(buf->queue_entry);
            kfree(buf->buf_desc);
            buf->buf_desc = NULL;
            buf->header = NULL;
            buf->queue_entry = NULL;
        }
        list_del(&buf->list);
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
    }
    spin_unlock_irqrestore(&d->buf_lock, flags);
}

static void c985_init_capture_filter(struct c985_poc *d)
{
    struct CCaptureFilter *cf = &d->capture_filter;
    struct _KSFILTER *kf = &d->capture_ksfilter;

    /* Initialize CppObject base */
    memset(cf, 0, sizeof(*cf));
    cf->base.m_dwWhoAmI = CCAPTUREFILTER_WHOAMI;  /* 0x103ea */
    cf->base.m_dwObjectAttributes = 0;
    cf->base.m_fInitialized = 1;
    spin_lock_init(&cf->base.m_spinlock);
    mutex_init(&cf->base.m_mutex);

    /* Wire filter to device */
    cf->m_pDevice = (struct CDevice *)d;
    cf->m_p_ks_filt = kf;
    cf->m_process_name = PROCESS_TYPE_UNKNOWN;

    /* Wire KSFILTER->Context to filter object */
    memset(kf, 0, sizeof(*kf));
    kf->Context = cf;
}

static void c985_init_encoder_filter(struct c985_poc *d)
{
    struct CEncoderFilter *ef = &d->encoder_filter;
    struct _KSFILTER *kf = &d->encoder_ksfilter;

    /* Initialize CppObject base */
    memset(ef, 0, sizeof(*ef));
    ef->base.m_dwWhoAmI = CENCODERFILTER_WHOAMI;  /* 0x103fc */
    ef->base.m_dwObjectAttributes = 0;
    ef->base.m_fInitialized = 1;
    spin_lock_init(&ef->base.m_spinlock);
    mutex_init(&ef->base.m_mutex);

    /* Wire filter to device */
    ef->m_pDevice = (struct CDevice *)d;
    ef->m_p_ks_filt = kf;
    ef->m_process_name = PROCESS_TYPE_UNKNOWN;

    /* Wire KSFILTER->Context to filter object */
    memset(kf, 0, sizeof(*kf));
    kf->Context = ef;
}

/* -----------------------------------------------------------------------
 * Main Streaming Function - Using Pin Constructors
 * --------------------------------------------------------------------- */

/* YUV pin uses the raw video GUID */
/* Raw video pin Name GUID: fb6c4281-0353-11d1-905f-0000c0cc16ba */
static _GUID yuv_pin_name_guid = {
    .Data1 = 0xfb6c4281,
    .Data2 = 0x0353,
    .Data3 = 0x11d1,
    .Data4 = { 0x90, 0x5f, 0x00, 0x00, 0xc0, 0xcc, 0x16, 0xba }
};

static struct _KSPIN_DESCRIPTOR_EX yuv_pin_descriptor = {
    .PinDescriptor = {
        .Name = &yuv_pin_name_guid,
    },
};

/* MPEG out pin Name GUID: 6da2460e-3021-425f-9dc5-7311a8aeb761 */
static _GUID comp_pin_name_guid = {
    .Data1 = 0x6da2460e,
    .Data2 = 0x3021,
    .Data3 = 0x425f,
    .Data4 = { 0x9d, 0xc5, 0x73, 0x11, 0xa8, 0xae, 0xb7, 0x61 }
};

static struct _KSPIN_DESCRIPTOR_EX comp_pin_descriptor = {
    .PinDescriptor = {
        .Name = &comp_pin_name_guid,
    },
};

static int c985_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct c985_poc *d = vb2_get_drv_priv(vq);
    long ret;
    struct KSDATAFORMAT yuv_format;
    struct KSDATAFORMAT comp_format;
    struct WAVEFORMATEX *yuv_fmt_ex;
    struct WAVEFORMATEX *comp_fmt_ex;

    SYNC_PRINT("start_streaming ENTER count=%u d=%px", count, d);

    dev_info(&d->pdev->dev, "start_streaming: count=%u\n", count);

    c985_init_streaming_state(d);
    SYNC_PRINT("SS1 init_streaming_state done");

    c985_init_capture_filter(d);
    SYNC_PRINT("SS2 init_capture_filter done");

    c985_init_encoder_filter(d);
    SYNC_PRINT("SS3 init_encoder_filter done");

    memset(&d->yuv_ks_pin, 0, sizeof(d->yuv_ks_pin));
    memset(&yuv_format, 0, sizeof(yuv_format));

    d->yuv_ks_pin._parent = &d->capture_ksfilter;
    d->yuv_ks_pin.Descriptor = &yuv_pin_descriptor;


    yuv_format.Size = sizeof(struct KSDATAFORMAT) + sizeof(struct WAVEFORMATEX);
    yuv_format.Flags = 0;
    yuv_format.SampleSize = 1920 * 1080 * 3 / 2;
    yuv_format.SubFormat[0] = 0x59;
    yuv_format.SubFormat[1] = 0x56;
    yuv_format.SubFormat[2] = 0x31;
    yuv_format.SubFormat[3] = 0x32;
    yuv_format.SubFormat[4] = 0x72;
    yuv_format.SubFormat[5] = 0xf0;
    yuv_format.SubFormat[6] = 0xca;
    yuv_format.SubFormat[7] = 0x40;
    yuv_format.SubFormat[8] = 0x82;
    yuv_format.SubFormat[9] = 0x9d;
    yuv_format.SubFormat[10] = 0x47;
    yuv_format.SubFormat[11] = 0xd5;
    yuv_format.SubFormat[12] = 0xd2;
    yuv_format.SubFormat[13] = 0x83;
    yuv_format.SubFormat[14] = 0x54;
    yuv_format.SubFormat[15] = 0x22;
    d->yuv_ks_pin.ConnectionFormat = &yuv_format;

    yuv_fmt_ex = (struct WAVEFORMATEX *)((u8 *)&yuv_format + sizeof(struct KSDATAFORMAT));
    yuv_fmt_ex->wFormatTag = 0;
    yuv_fmt_ex->nChannels = 2;
    yuv_fmt_ex->nSamplesPerSec = 48000;
    yuv_fmt_ex->nAvgBytesPerSec = 192000;
    yuv_fmt_ex->nBlockAlign = 4;
    yuv_fmt_ex->wBitsPerSample = 16;

    d->yuv_ks_pin.Id = 0;
    d->yuv_ks_pin.Communication = KSPIN_COMMUNICATION_SINK;
    d->yuv_ks_pin.DataFlow = KSPIN_DATAFLOW_IN;
    d->yuv_ks_pin.DeviceState = 0;

    {
        struct tagKS_BITMAPINFOHEADER *bmi =
        (struct tagKS_BITMAPINFOHEADER *)((u8 *)&yuv_format + 0x38);
        bmi->biSize = sizeof(struct tagKS_BITMAPINFOHEADER);
        bmi->biWidth = 1920;
        bmi->biHeight = 1080;
        bmi->biPlanes = 1;
        bmi->biBitCount = 12;
        bmi->biCompression = 0;
        bmi->biSizeImage = 1920 * 1080 * 3 / 2;
        bmi->biXPelsPerMeter = 0;
        bmi->biYPelsPerMeter = 0;
        bmi->biClrUsed = 0;
        bmi->biClrImportant = 0;
    }

    SYNC_PRINT("SS4 yuv format setup done");

    memset(&d->yuv_pin, 0, sizeof(d->yuv_pin));
    SYNC_PRINT("SS5 yuv_pin memset done sizeof=%zu", sizeof(d->yuv_pin));

    ret = 0;
    CYUVOutPin_CYUVOutPin(&d->yuv_pin, &d->yuv_ks_pin,
                          (struct CDevice *)d, 0, 0, 5, &ret);
    SYNC_PRINT("SS6 CYUVOutPin returned ret=%ld", ret);

    if (ret < 0) {
        dev_err(&d->pdev->dev, "CYUVOutPin constructor failed: %ld\n", ret);
        goto err_cleanup;
    }

    dev_info(&d->pdev->dev, "CYUVOutPin created: format %dx%d\n",
             d->yuv_pin.m_info_hdr.bmiHeader.biWidth,
             d->yuv_pin.m_info_hdr.bmiHeader.biHeight);

    SYNC_PRINT("SS7 about to CDataPin_Create YUV");

    ret = CDataPin_Create(&d->yuv_pin.base);
    SYNC_PRINT("SS8 CDataPin_Create YUV ret=%ld", ret);

    if (ret < 0) {
        dev_err(&d->pdev->dev, "CDataPin::Create (YUV) failed: %ld\n", ret);
        goto err_cleanup;
    }
    dev_info(&d->pdev->dev, "CDataPin::Create (YUV) complete\n");

    memset(&d->comp_ks_pin, 0, sizeof(d->comp_ks_pin));
    memset(&comp_format, 0, sizeof(comp_format));

    d->comp_ks_pin._parent = &d->encoder_ksfilter;
    d->comp_ks_pin.Descriptor = &comp_pin_descriptor;


    comp_format.Size = sizeof(struct KSDATAFORMAT) + sizeof(struct WAVEFORMATEX);
    comp_format.Flags = 0;
    comp_format.SampleSize = 512 * 1024;
    d->comp_ks_pin.ConnectionFormat = &comp_format;

    comp_fmt_ex = (struct WAVEFORMATEX *)((u8 *)&comp_format + sizeof(struct KSDATAFORMAT));
    comp_fmt_ex->wFormatTag = 0;
    comp_fmt_ex->nChannels = 2;
    comp_fmt_ex->nSamplesPerSec = 48000;
    comp_fmt_ex->nAvgBytesPerSec = 192000;
    comp_fmt_ex->nBlockAlign = 4;
    comp_fmt_ex->wBitsPerSample = 16;

    d->comp_ks_pin.Id = 1;
    d->comp_ks_pin.Communication = KSPIN_COMMUNICATION_SINK;
    d->comp_ks_pin.DataFlow = KSPIN_DATAFLOW_OUT;
    d->comp_ks_pin.DeviceState = 0;

    {
        struct WAVEFORMATEX *fmt =
        (struct WAVEFORMATEX *)((u8 *)&comp_format + 0x40);
        fmt->wFormatTag = 0;
        fmt->nChannels = 2;
        fmt->nSamplesPerSec = 48000;
        fmt->nAvgBytesPerSec = 192000;
        fmt->nBlockAlign = 4;
        fmt->wBitsPerSample = 16;
    }

    SYNC_PRINT("SS9 comp format setup done");

    memset(&d->comp_pin, 0, sizeof(d->comp_pin));
    SYNC_PRINT("SS10 comp_pin memset done sizeof=%zu", sizeof(d->comp_pin));

    ret = 0;
    CPCMOutPin_CPCMOutPin(&d->comp_pin, &d->comp_ks_pin,
                          (struct CDevice *)d, 0, 0, 0, &ret);
    SYNC_PRINT("SS11 CPCMOutPin returned ret=%ld", ret);

    if (ret < 0) {
        dev_err(&d->pdev->dev, "CPCMOutPin constructor failed: %ld\n", ret);
        goto err_cleanup;
    }

    dev_info(&d->pdev->dev, "CPCMOutPin created\n");

    SYNC_PRINT("SS12 about to CDataPin_Create COMP");

    /* DIAGNOSTIC START */
    {
        struct _KSPIN *kspin = d->comp_pin.base.base.m_p_ks_pin;
        SYNC_PRINT("DIAG: comp_pin.base.base.m_p_ks_pin=%px", kspin);
        SYNC_PRINT("DIAG: &d->comp_ks_pin=%px", &d->comp_ks_pin);
        if (kspin) {
            struct _KSFILTER *parent = kspin->_parent;
            SYNC_PRINT("DIAG: kspin->_parent=%px", parent);
            SYNC_PRINT("DIAG: &d->encoder_ksfilter=%px", &d->encoder_ksfilter);
            if (parent) {
                void *ctx = parent->Context;
                SYNC_PRINT("DIAG: parent->Context=%px", ctx);
                SYNC_PRINT("DIAG: &d->encoder_filter=%px", &d->encoder_filter);
                if (ctx) {
                    struct CEncoderFilter *ef = (struct CEncoderFilter *)ctx;
                    SYNC_PRINT("DIAG: ef->m_pDevice=%px", ef->m_pDevice);
                    SYNC_PRINT("DIAG: d=%px", d);
                }
            }
        }
    }
    /* DIAGNOSTIC END */

    ret = CDataPin_Create(&d->comp_pin.base);
    SYNC_PRINT("SS13 CDataPin_Create COMP ret=%ld", ret);

    if (ret < 0) {
        dev_err(&d->pdev->dev, "CDataPin::Create (COMP) failed: %ld\n", ret);
        goto err_cleanup;
    }
    dev_info(&d->pdev->dev, "CDataPin::Create (COMP) complete\n");

    d->encoder_running = true;
    dev_info(&d->pdev->dev, "Encoder started with YUV and COMP pins\n");

    SYNC_PRINT("SS14 start_streaming COMPLETE");

    return 0;

    err_cleanup:
    SYNC_PRINT("SS_ERR start_streaming cleanup ret=%ld", ret);
    c985_cleanup_on_error(d);
    return ret < 0 ? ret : -EIO;
}

static const struct vb2_ops c985_vb2_ops = {
    .queue_setup     = c985_queue_setup,
    .buf_prepare     = c985_buf_prepare,
    .buf_queue       = c985_buf_queue,
    .start_streaming = c985_start_streaming,
    .stop_streaming  = c985_stop_streaming,
};


static int c985_ioctl_reqbufs(struct file *file, void *priv,
                              struct v4l2_requestbuffers *p)
{
    pr_emerg("C985_IOC: REQBUFS enter count=%u type=%u memory=%u\n",
             p->count, p->type, p->memory);
    mdelay(100);

    int ret = vb2_ioctl_reqbufs(file, priv, p);

    pr_emerg("C985_IOC: REQBUFS exit ret=%d count=%u\n", ret, p->count);
    mdelay(100);
    return ret;
}

static int c985_ioctl_qbuf(struct file *file, void *priv,
                           struct v4l2_buffer *p)
{
    pr_emerg("C985_IOC: QBUF enter index=%u type=%u memory=%u\n",
             p->index, p->type, p->memory);
    mdelay(100);

    int ret = vb2_ioctl_qbuf(file, priv, p);

    pr_emerg("C985_IOC: QBUF exit ret=%d index=%u\n", ret, p->index);
    mdelay(100);
    return ret;
}

static int c985_ioctl_streamon(struct file *file, void *priv,
                               enum v4l2_buf_type type)
{
    pr_emerg("C985_IOC: STREAMON enter type=%u\n", type);
    mdelay(100);

    int ret = vb2_ioctl_streamon(file, priv, type);

    pr_emerg("C985_IOC: STREAMON exit ret=%d\n", ret);
    mdelay(100);
    return ret;
}

static int c985_ioctl_streamoff(struct file *file, void *priv,
                                enum v4l2_buf_type type)
{
    pr_emerg("C985_IOC: STREAMOFF enter type=%u\n", type);
    mdelay(100);

    int ret = vb2_ioctl_streamoff(file, priv, type);

    pr_emerg("C985_IOC: STREAMOFF exit ret=%d\n", ret);
    mdelay(100);
    return ret;
}

static const struct v4l2_ioctl_ops c985_ioctl_ops = {
    .vidioc_querycap         = c985_querycap,
    .vidioc_enum_input       = c985_enum_input,
    .vidioc_g_input          = c985_g_input,
    .vidioc_s_input          = c985_s_input,
    .vidioc_enum_fmt_vid_cap = c985_enum_fmt,
    .vidioc_g_fmt_vid_cap    = c985_g_fmt,
    .vidioc_s_fmt_vid_cap    = c985_s_fmt,
    .vidioc_try_fmt_vid_cap  = c985_try_fmt,
    .vidioc_g_parm           = c985_g_parm,
    .vidioc_s_parm           = c985_s_parm,
    .vidioc_reqbufs          = c985_ioctl_reqbufs,
    .vidioc_querybuf         = vb2_ioctl_querybuf,
    .vidioc_qbuf             = c985_ioctl_qbuf,
    .vidioc_dqbuf            = vb2_ioctl_dqbuf,
    .vidioc_streamon         = c985_ioctl_streamon,
    .vidioc_streamoff        = c985_ioctl_streamoff,
};
int c985_v4l2_register(struct c985_poc *d)
{

    struct vb2_queue *vq;
    int ret;

    dev_info(&d->pdev->dev, "registering V4L2 device\n");

    d->width = 1920;
    d->height = 1080;

    INIT_LIST_HEAD(&d->buf_list);
    INIT_LIST_HEAD(&d->pending_frames);
    spin_lock_init(&d->buf_lock);

    ret = kfifo_alloc(&d->frame_fifo, 1024 * 1024, GFP_KERNEL);
    if (ret) {
        dev_err(&d->pdev->dev, "kfifo_alloc failed: %d\n", ret);
        return ret;
    }

    ret = v4l2_device_register(&d->pdev->dev, &d->v4l2_dev);
    if (ret) {
        dev_err(&d->pdev->dev, "v4l2_device_register failed: %d\n", ret);
        goto err_kfifo;
    }

    mutex_init(&d->v4l2_lock);

    vq = &d->vb2_queue;
    SYNC_PRINT("Initializing vq at %px (d=%px)", vq, d);

    vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vq->io_modes = VB2_MMAP;
    vq->drv_priv = d;
    vq->buf_struct_size = sizeof(struct c985_buffer);
    vq->ops = &c985_vb2_ops;
    vq->mem_ops = &vb2_dma_contig_memops;
    vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    vq->lock = &d->v4l2_lock;

    /* CRITICAL: Set device pointer for DMA-contig allocator */
    vq->dev = &d->pdev->dev;

    ret = vb2_queue_init(vq);
    SYNC_PRINT("vb2_queue_init returned %d", ret);

    if (ret) {
        dev_err(&d->pdev->dev, "vb2_queue_init failed: %d\n", ret);
        goto err_mutex;
    }

    memset(&d->vdev, 0, sizeof(d->vdev));

    strscpy(d->vdev.name, "AVerMedia C985 HDMI", sizeof(d->vdev.name));
    d->vdev.release = video_device_release_empty;
    d->vdev.fops = &c985_fops;
    d->vdev.ioctl_ops = &c985_ioctl_ops;
    d->vdev.v4l2_dev = &d->v4l2_dev;
    d->vdev.queue = vq;
    d->vdev.lock = &d->v4l2_lock;
    d->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
    video_set_drvdata(&d->vdev, d);

    ret = video_register_device(&d->vdev, VFL_TYPE_VIDEO, -1);
    if (ret) {
        dev_err(&d->pdev->dev, "video_register_device failed: %d\n", ret);
        goto err_mutex;
    }

    d->v4l2_registered = true;

    dev_info(&d->pdev->dev, "V4L2 device registered as /dev/video%d\n", d->vdev.num);

    return 0;

    err_mutex:
    mutex_destroy(&d->v4l2_lock);
    v4l2_device_unregister(&d->v4l2_dev);
    err_kfifo:
    kfifo_free(&d->frame_fifo);
    return ret;
}
