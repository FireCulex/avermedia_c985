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

/* VIDEO_MAX_FRAME may not be defined in all kernel versions */
#ifndef VIDEO_MAX_FRAME
#define VIDEO_MAX_FRAME 32
#endif

#include "pins.h"
#include "structs.h"
#include "v4l2.h"

#define C985_V4L2_NAME "c985-video"

/* -----------------------------------------------------------------------
 * Videobuf2 operations
 * --------------------------------------------------------------------- */

static int c985_queue_setup(struct vb2_queue *vq,
                            unsigned int *nbuffers, unsigned int *nplanes,
                            unsigned int sizes[], struct device *alloc_devs[])
{
    unsigned int size = 1920 * 1080 * 3 / 2;  /* ~3MB for raw YUV */

    if (*nplanes)
        return sizes[0] < size ? -EINVAL : 0;

    *nplanes = 1;
    sizes[0] = size;
    *nbuffers = min(*nbuffers, 8u);

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
    struct c985_poc *d = vb2_get_drv_priv(vb->vb2_queue);
    struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
    struct c985_buffer *buf = container_of(vbuf, struct c985_buffer, vb);
    unsigned long flags;

    spin_lock_irqsave(&d->buf_lock, flags);

    if (!buf->queue_entry) {
        struct _QP_BUFFER_DESCRIPTOR *desc;
        struct _QP_KSSTREAM_HEADER *header;
        struct QUEUE_ENTRY *entry;

        desc = kzalloc(sizeof(struct _QP_BUFFER_DESCRIPTOR), GFP_ATOMIC);
        header = kzalloc(sizeof(struct _QP_KSSTREAM_HEADER), GFP_ATOMIC);
        entry = kzalloc(sizeof(struct QUEUE_ENTRY), GFP_ATOMIC);

        if (!desc || !header || !entry) {
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

        /* CPU virtual address for driver to read after DMA */
        header->Data = vb2_plane_vaddr(vb, 0);
        header->FrameExtent = desc->ulBufferSize;
        header->DataUsed = 0;

        buf->buf_desc = desc;
        buf->header = header;
        buf->queue_entry = entry;

        entry->Data = desc;
        entry->pNext = NULL;
    }

    list_add_tail(&buf->list, &d->buf_list);

    spin_unlock_irqrestore(&d->buf_lock, flags);
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
    inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;

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
    .vidioc_reqbufs          = vb2_ioctl_reqbufs,
    .vidioc_querybuf         = vb2_ioctl_querybuf,
    .vidioc_qbuf             = vb2_ioctl_qbuf,
    .vidioc_dqbuf            = vb2_ioctl_dqbuf,
    .vidioc_streamon         = vb2_ioctl_streamon,
    .vidioc_streamoff        = vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------
 * V4L2 file operations
 * --------------------------------------------------------------------- */

static const struct v4l2_file_operations c985_fops = {
    .owner          = THIS_MODULE,
    .open           = v4l2_fh_open,
    .release        = vb2_fop_release,
    .read           = vb2_fop_read,
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

/* -----------------------------------------------------------------------
 * Main Streaming Function - Using Pin Constructors
 * --------------------------------------------------------------------- */

static int c985_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct c985_poc *d = vb2_get_drv_priv(vq);
    long ret;
    struct _KSPIN yuv_ks_pin;
    struct _KSPIN comp_ks_pin;
    struct KSDATAFORMAT yuv_format;
    struct KSDATAFORMAT comp_format;
    struct WAVEFORMATEX *yuv_fmt_ex;
    struct WAVEFORMATEX *comp_fmt_ex;

    dev_info(&d->pdev->dev, "start_streaming: count=%u\n", count);

    c985_init_streaming_state(d);

    /*
     * Step 1: Initialize KSPIN structures for YUV input
     *
     * Instead of CTask_Open, we directly construct the pin objects.
     * The KSPIN structure represents a kernel streaming pin connection.
     */
    memset(&yuv_ks_pin, 0, sizeof(yuv_ks_pin));
    memset(&yuv_format, 0, sizeof(yuv_format));

    /* Set up KSDATAFORMAT for YUV (video input) */
    yuv_format.Size = sizeof(struct KSDATAFORMAT) + sizeof(struct WAVEFORMATEX);
    yuv_format.Flags = 0;
    yuv_format.SampleSize = 1920 * 1080 * 3 / 2;  /* YUV frame size */
    /* Set SubFormat to YV12 GUID: 32315659-f072-40ca-829d-47d5d2835422 */
    yuv_format.SubFormat[0] = 0x59;  /* 'Y' */
    yuv_format.SubFormat[1] = 0x56;  /* 'V' */
    yuv_format.SubFormat[2] = 0x31;  /* '1' */
    yuv_format.SubFormat[3] = 0x32;  /* '2' */
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
    yuv_ks_pin.ConnectionFormat = &yuv_format;

    /* Set up WAVEFORMATEX extension for YUV format
     * Windows: nChannels=2, nSamplesPerSec=48000, nAvgBytesPerSec=192000, nBlockAlign=4, wBitsPerSample=16
     */
    yuv_fmt_ex = (struct WAVEFORMATEX *)((u8 *)&yuv_format + sizeof(struct KSDATAFORMAT));
    yuv_fmt_ex->wFormatTag = 0;  /* YUV format */
    yuv_fmt_ex->nChannels = 2;   /* Y + UV planes */
    yuv_fmt_ex->nSamplesPerSec = 48000;  /* Audio sample rate */
    yuv_fmt_ex->nAvgBytesPerSec = 192000;
    yuv_fmt_ex->nBlockAlign = 4;
    yuv_fmt_ex->wBitsPerSample = 16;

    /* Set KSPIN properties for YUV input */
    yuv_ks_pin.Id = 0;  /* Pin ID 0 for YUV input */
    yuv_ks_pin.Communication = KSPIN_COMMUNICATION_SINK;
    yuv_ks_pin.DataFlow = KSPIN_DATAFLOW_IN;
    yuv_ks_pin.DeviceState = 0;  /* Active */

    /* CRITICAL: Initialize the format-specific data
     * CYUVOutPin copies ConnectionFormat[8..95] to local_78, then to m_info_hdr
     * m_info_hdr layout: rcSource(0x00), rcTarget(0x10), dwBitRate(0x20), dwBitErrorRate(0x24),
     *   AvgTimePerFrame(0x28), bmiHeader(0x30)
     * bmiHeader layout: biSize(0x00), biWidth(0x04), biHeight(0x08), biPlanes(0x0C),
     *   biBitCount(0x0E), biCompression(0x10), biSizeImage(0x14)
     * So bmiHeader must be at ConnectionFormat[8 + 0x30] = ConnectionFormat[0x38]
     * Match Windows: w(1920) h(1080) bits(12) fr(30) dataType(1=YV12)
     */
    {
        struct tagKS_BITMAPINFOHEADER *bmi = (struct tagKS_BITMAPINFOHEADER *)((u8 *)&yuv_format + 0x38);
        bmi->biSize = sizeof(struct tagKS_BITMAPINFOHEADER);          /* 0x00 */
        bmi->biWidth = 1920;                                          /* 0x04 */
        bmi->biHeight = 1080;                                         /* 0x08 */
        bmi->biPlanes = 1;                                            /* 0x0C */
        bmi->biBitCount = 12;                                         /* 0x0E - YUV420 */
        bmi->biCompression = 0;                                       /* 0x10 - Raw YUV */
        bmi->biSizeImage = 1920 * 1080 * 3 / 2;                       /* 0x14 */
        bmi->biXPelsPerMeter = 0;                                     /* 0x18 */
        bmi->biYPelsPerMeter = 0;                                     /* 0x1C */
        bmi->biClrUsed = 0;                                           /* 0x20 */
        bmi->biClrImportant = 0;                                      /* 0x24 */
    }

    /*
     * Step 2: Construct CYUVOutPin for YUV input
     *
     * CYUVOutPin handles the raw YUV data from HDMI capture.
     * param_5 is hardcoded to 5 (video pin type) in the constructor.
     */
    memset(&d->yuv_pin, 0, sizeof(d->yuv_pin));
    ret = 0;
    CYUVOutPin_CYUVOutPin(&d->yuv_pin, &yuv_ks_pin, (struct c_device *)d, 0, 0, 5, &ret);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "CYUVOutPin constructor failed: %ld\n", ret);
        goto err_cleanup;
    }

    dev_info(&d->pdev->dev, "CYUVOutPin created: format %dx%d\n",
             d->yuv_pin.m_info_hdr.bmiHeader.biWidth,
             d->yuv_pin.m_info_hdr.bmiHeader.biHeight);

    /* Call CDataPin::Create for YUV pin */
    ret = CDataPin_Create(&d->yuv_pin.base);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "CDataPin::Create (YUV) failed: %ld\n", ret);
        goto err_cleanup;
    }
    dev_info(&d->pdev->dev, "CDataPin::Create (YUV) complete\n");

    /*
     * Step 3: Initialize KSPIN structures for compressed video output
     *
     * CPCMOutPin handles the H.264 encoded output stream.
     */
    memset(&comp_ks_pin, 0, sizeof(comp_ks_pin));
    memset(&comp_format, 0, sizeof(comp_format));

    /* Set up KSDATAFORMAT for compressed video (H.264 output) */
    comp_format.Size = sizeof(struct KSDATAFORMAT) + sizeof(struct WAVEFORMATEX);
    comp_format.Flags = 0;
    comp_format.SampleSize = 512 * 1024;  /* Max H.264 frame size */
    comp_ks_pin.ConnectionFormat = &comp_format;

    /* Set up WAVEFORMATEX extension for H.264
     * Windows: nChannels=2, nSamplesPerSec=48000, nAvgBytesPerSec=192000, nBlockAlign=4, wBitsPerSample=16
     */
    comp_fmt_ex = (struct WAVEFORMATEX *)((u8 *)&comp_format + sizeof(struct KSDATAFORMAT));
    comp_fmt_ex->wFormatTag = 0;  /* Compressed format */
    comp_fmt_ex->nChannels = 2;
    comp_fmt_ex->nSamplesPerSec = 48000;
    comp_fmt_ex->nAvgBytesPerSec = 192000;
    comp_fmt_ex->nBlockAlign = 4;
    comp_fmt_ex->wBitsPerSample = 16;

    /* Set KSPIN properties for compressed video output */
    comp_ks_pin.Id = 1;  /* Pin ID 1 for compressed output */
    comp_ks_pin.Communication = KSPIN_COMMUNICATION_SINK;
    comp_ks_pin.DataFlow = KSPIN_DATAFLOW_OUT;
    comp_ks_pin.DeviceState = 0;  /* Active */

    /* CRITICAL: Initialize format-specific data for CPCMOutPin
     * CPCMOutPin reads at offsets:
     *   nChannels(0x42), nSamplesPerSec(0x44), nAvgBytesPerSec(0x48), nBlockAlign(0x4C), nBitsPerSample(0x4E)
     * WAVEFORMATEX layout:
     *   wFormatTag(0x00), nChannels(0x02), nSamplesPerSec(0x04), nAvgBytesPerSec(0x08), nBlockAlign(0x0C), wBitsPerSample(0x0E)
     * So WAVEFORMATEX starts at offset 0x40 (nChannels at 0x40+0x02=0x42)
     * Match Windows: nChannels(2) nSamplesPerSec(48000) nAvgBytesPerSec(192000) nBlockAlign(4) nBitsPerSample(16)
     */
    {
        struct WAVEFORMATEX *fmt = (struct WAVEFORMATEX *)((u8 *)&comp_format + 0x40);
        fmt->wFormatTag = 0;
        fmt->nChannels = 2;
        fmt->nSamplesPerSec = 48000;
        fmt->nAvgBytesPerSec = 192000;
        fmt->nBlockAlign = 4;
        fmt->wBitsPerSample = 16;
    }

    /*
     * Step 4: Construct CPCMOutPin for compressed video output
     *
     * CPCMOutPin handles the encoded H.264 stream.
     * param_5 = 0 (not a wave input filter)
     */
    memset(&d->comp_pin, 0, sizeof(d->comp_pin));
    ret = 0;
    CPCMOutPin_CPCMOutPin(&d->comp_pin, &comp_ks_pin, (struct c_device *)d, 0, 0, 0, &ret);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "CPCMOutPin constructor failed: %ld\n", ret);
        goto err_cleanup;
    }

    dev_info(&d->pdev->dev, "CPCMOutPin created: sample rate %u Hz\n",
             ((struct QP_PCM_DATAFORMAT *)d->comp_pin.base.base._padding_[16])->nSamplesPerSec);

    /* Call CDataPin::Create for COMP pin */
    ret = CDataPin_Create(&d->comp_pin.base);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "CDataPin::Create (COMP) failed: %ld\n", ret);
        goto err_cleanup;
    }
    dev_info(&d->pdev->dev, "CDataPin::Create (COMP) complete\n");

    d->encoder_running = true;
    dev_info(&d->pdev->dev, "Encoder started with YUV and COMP pins\n");

    return 0;

    err_cleanup:
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
    vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vq->io_modes = VB2_MMAP | VB2_READ;
    vq->drv_priv = d;
    vq->buf_struct_size = sizeof(struct c985_buffer);
    vq->ops = &c985_vb2_ops;
    vq->mem_ops = &vb2_dma_contig_memops;
    vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    vq->lock = &d->v4l2_lock;

    /* CRITICAL: Set device pointer for DMA-contig allocator */
    vq->dev = &d->pdev->dev;

    ret = vb2_queue_init(vq);
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
    d->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
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
