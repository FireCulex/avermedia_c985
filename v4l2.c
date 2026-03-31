// SPDX-License-Identifier: GPL-2.0
// v4l2.c — V4L2 video capture interface for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-vmalloc.h>
#include "cpr.h"

#include "avermedia_c985.h"
#include "v4l2.h"
#include "nuc100.h"
#include "qpfwencapi.h"
#include <linux/delay.h>

#define C985_V4L2_NAME "c985-video"

/* -----------------------------------------------------------------------
 * Videobuf2 operations
 * --------------------------------------------------------------------- */

static int c985_queue_setup(struct vb2_queue *vq,
                            unsigned int *nbuffers, unsigned int *nplanes,
                            unsigned int sizes[], struct device *alloc_devs[])
{
    unsigned int size = 512 * 1024;  /* 512KB max per H.264 frame */

    if (*nplanes)
        return sizes[0] < size ? -EINVAL : 0;

    *nplanes = 1;
    sizes[0] = size;
    *nbuffers = min(*nbuffers, 8u);

    return 0;
}

static int c985_buf_prepare(struct vb2_buffer *vb)
{
    unsigned int size = 512 * 1024;

    if (vb2_plane_size(vb, 0) < size)
        return -EINVAL;

    /* Don't set payload here - set it when we have actual data */
    return 0;
}


static int c985_start_streaming(struct vb2_queue *vq, unsigned int count)
{
    struct c985_poc *d = vb2_get_drv_priv(vq);
    struct c985_buffer *buf, *tmp;
    unsigned long flags;
    int ret;

    dev_info(&d->pdev->dev, "start_streaming: count=%u\n", count);

    d->sequence = 0;

    ret = qpfwencapi_start(d);
    if (ret) {
        dev_err(&d->pdev->dev, "encoder start failed: %d\n", ret);

        spin_lock_irqsave(&d->buf_lock, flags);
        list_for_each_entry_safe(buf, tmp, &d->buf_list, list) {
            list_del(&buf->list);
            vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
        }
        spin_unlock_irqrestore(&d->buf_lock, flags);
        return ret;
    }

    /* That's it - no blocking diagnostics */
    return 0;
}

static void c985_stop_streaming(struct vb2_queue *vq)
{
    struct c985_poc *d = vb2_get_drv_priv(vq);
    struct c985_buffer *buf, *tmp;
    unsigned long flags;

    dev_info(&d->pdev->dev, "stop_streaming\n");

    qpfwencapi_stop(d);
    msleep(100);

    spin_lock_irqsave(&d->buf_lock, flags);
    list_for_each_entry_safe(buf, tmp, &d->buf_list, list) {
        list_del(&buf->list);
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
    }
    spin_unlock_irqrestore(&d->buf_lock, flags);

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
    list_add_tail(&buf->list, &d->buf_list);
    spin_unlock_irqrestore(&d->buf_lock, flags);
}



static const struct vb2_ops c985_vb2_ops = {
    .queue_setup     = c985_queue_setup,
    .buf_prepare     = c985_buf_prepare,
    .buf_queue       = c985_buf_queue,
    .start_streaming = c985_start_streaming,
    .stop_streaming  = c985_stop_streaming,
    .wait_prepare    = vb2_ops_wait_prepare,
    .wait_finish     = vb2_ops_wait_finish,
};

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
    int ret;

    ret = nuc100_get_hdmi_status(d);
    if (ret > 0) {
        struct nuc100_hdmi_timing t;
        int valid;
        nuc100_get_hdmi_timing(d, &t, &valid);
    } else {
        d->width = 1920;
        d->height = 1080;
    }

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

/* -----------------------------------------------------------------------
 * Init / Exit
 * --------------------------------------------------------------------- */

int c985_v4l2_register(struct c985_poc *d)
{
    struct vb2_queue *vq;
    int ret;

    dev_info(&d->pdev->dev, "registering V4L2 device\n");

    d->width = 1920;
    d->height = 1080;

    INIT_LIST_HEAD(&d->buf_list);
    spin_lock_init(&d->buf_lock);

    ret = v4l2_device_register(&d->pdev->dev, &d->v4l2_dev);
    if (ret) {
        dev_err(&d->pdev->dev, "v4l2_device_register failed: %d\n", ret);
        return ret;
    }

    mutex_init(&d->v4l2_lock);

    vq = &d->vb2_queue;
    vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vq->io_modes = VB2_MMAP | VB2_READ;
    vq->drv_priv = d;
    vq->buf_struct_size = sizeof(struct c985_buffer);
    vq->ops = &c985_vb2_ops;
    vq->mem_ops = &vb2_vmalloc_memops;
    vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
    vq->lock = &d->v4l2_lock;

    ret = vb2_queue_init(vq);
    if (ret) {
        dev_err(&d->pdev->dev, "vb2_queue_init failed: %d\n", ret);
        goto err_v4l2;
    }

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
        goto err_v4l2;
    }

    dev_info(&d->pdev->dev, "V4L2 device registered as /dev/video%d\n", d->vdev.num);

    return 0;

    err_v4l2:
    v4l2_device_unregister(&d->v4l2_dev);
    return ret;
}

void c985_v4l2_unregister(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "unregistering V4L2 device\n");

    video_unregister_device(&d->vdev);
    v4l2_device_unregister(&d->v4l2_dev);
}


