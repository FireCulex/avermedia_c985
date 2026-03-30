// SPDX-License-Identifier: GPL-2.0
/*
 * encoder.c - H.264 encoder task and frame capture for AVerMedia C985
 *
 * Handles ARM messages for encoded frame notifications (cmd 0x40/0x41)
 * and manages the frame info FIFO for DMA transfer scheduling.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>

#include "avermedia_c985.h"
#include "encoder.h"
#include "cpr.h"

#include "pciecntl.h"
#include <media/videobuf2-v4l2.h>

/*
 * Data type codes from ARM message param1 & 0xFFFF
 */
#define ARM_DATATYPE_COMP_VID   0x0001  /* Compressed video (H.264) */
#define ARM_DATATYPE_COMP_AUD   0x0002  /* Compressed audio (AAC) */
#define ARM_DATATYPE_YUV        0x0081  /* Raw YUV video */
#define ARM_DATATYPE_PCM        0x0082  /* Raw PCM audio */

/*
 * Frame type codes (local_1ac in decompile)
 */
#define FRAME_TYPE_YUV_PLANAR   1
#define FRAME_TYPE_YUV_PACKED   2
#define FRAME_TYPE_COMPRESSED   3
#define FRAME_TYPE_OTHER        4

/**
 * encoder_frame_work_handler - Work function to process pending frames
 * @work: work struct embedded in c985_poc
 *
 * Dequeues frames from FIFO, gets V4L2 buffer, DMAs data, returns to userspace.
 */
static void encoder_frame_work_handler(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, frame_work);
    struct frame_info frame;
    struct c985_buffer *buf;
    unsigned long flags;
    void *vbuf;
    int ret;

    while (encoder_get_pending_frame(d, &frame) == 0) {
        dev_dbg(&d->pdev->dev,
                "WORK: processing frame seq=%u addr=0x%08x size=%u\n",
                frame.sequence, frame.card_addr, frame.size);

        /* Get a V4L2 buffer from the queue */
        spin_lock_irqsave(&d->buf_lock, flags);
        if (list_empty(&d->buf_list)) {
            spin_unlock_irqrestore(&d->buf_lock, flags);
            dev_warn(&d->pdev->dev, "WORK: no V4L2 buffer available, dropping frame\n");
            continue;
        }
        buf = list_first_entry(&d->buf_list, struct c985_buffer, list);
        list_del(&buf->list);
        spin_unlock_irqrestore(&d->buf_lock, flags);

        /* Get pointer to buffer memory */
        vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
        if (!vbuf) {
            dev_err(&d->pdev->dev, "WORK: failed to get buffer address\n");
            vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
            continue;
        }

        /* DMA from card RAM to host buffer */
        ret = pciecntl_start_dma_read(d, frame.card_addr << 2, vbuf, frame.size, true);
        if (ret) {
            dev_err(&d->pdev->dev, "WORK: DMA failed: %d\n", ret);
            vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
            continue;
        }

        /* Set buffer metadata */
        vb2_set_plane_payload(&buf->vb.vb2_buf, 0, frame.size);
        buf->vb.vb2_buf.timestamp = ktime_get_ns();
        buf->vb.sequence = frame.sequence;

        if (frame.keyframe)
            buf->vb.flags |= V4L2_BUF_FLAG_KEYFRAME;
        else
            buf->vb.flags &= ~V4L2_BUF_FLAG_KEYFRAME;

        /* Return buffer to userspace */
        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);

        dev_dbg(&d->pdev->dev,
                "WORK: delivered frame seq=%u size=%u key=%d\n",
                frame.sequence, frame.size, frame.keyframe);
    }
}

/**
 * encoder_init - Initialize encoder state
 * @d: device context
 *
 * Allocates and initializes the frame info FIFO and related state.
 */
int encoder_init(struct c985_poc *d)
{
    int ret;

    spin_lock_init(&d->frame_lock);
    INIT_LIST_HEAD(&d->pending_frames);
    INIT_WORK(&d->frame_work, encoder_frame_work_handler);

    ret = kfifo_alloc(&d->frame_fifo, ENCODER_FIFO_SIZE * sizeof(struct frame_info),
                      GFP_KERNEL);
    if (ret) {
        dev_err(&d->pdev->dev, "Failed to allocate frame FIFO\n");
        return ret;
    }

    d->frame_sequence = 0;
    d->encoder_running = false;

    dev_info(&d->pdev->dev, "Encoder initialized, FIFO size=%u frames\n",
             ENCODER_FIFO_SIZE);

    return 0;
}

/**
 * encoder_cleanup - Free encoder resources
 * @d: device context
 */
void encoder_cleanup(struct c985_poc *d)
{
    cancel_work_sync(&d->frame_work);
    kfifo_free(&d->frame_fifo);
}

/**
 * encoder_parse_arm_message - Parse frame notification from ARM
 * @d: device context
 * @cmd: command byte (0x40 = video, 0x41 = audio)
 * @p1: parameter 1 (data type, flags)
 * @p2: parameter 2 (card RAM address)
 * @p3: parameter 3 (secondary address / size info)
 * @p4: parameter 4 (frame size in bytes)
 * @p5: parameter 5 (PTS | keyframe flag)
 *
 * Called from IRQ handler when cmd 0x40 or 0x41 is received.
 * Parses the frame info and queues it for DMA transfer.
 *
 * Returns 0 on success, negative on error.
 */
int encoder_parse_arm_message(struct c985_poc *d,
                              u8 cmd, u32 p1, u32 p2, u32 p3, u32 p4, u32 p5)
{
    struct frame_info frame;
    u16 data_type;
    int frame_type;
    unsigned long flags;

    data_type = p1 & 0xFFFF;

    dev_dbg(&d->pdev->dev,
            "ENC: parse cmd=0x%02x type=0x%04x addr=0x%08x size=%u pts=0x%08x\n",
            cmd, data_type, p2, p4, p5);

    /*
     * Determine frame type from data_type field.
     * For our chip type (m_ChipType & 0xe) != 0, addresses are direct.
     */
    switch (data_type) {
        case ARM_DATATYPE_COMP_VID:
            frame_type = FRAME_TYPE_COMPRESSED;
            break;
        case ARM_DATATYPE_COMP_AUD:
            frame_type = FRAME_TYPE_COMPRESSED;
            break;
        case ARM_DATATYPE_YUV:
            frame_type = FRAME_TYPE_YUV_PLANAR;
            break;
        default:
            frame_type = FRAME_TYPE_OTHER;
            break;
    }

    /* Skip non-video frames for now */
    if (cmd != 0x40) {
        dev_dbg(&d->pdev->dev, "ENC: skipping audio frame (cmd=0x%02x)\n", cmd);
        return 0;
    }

    /* Skip if encoder not running */
    if (!d->encoder_running) {
        dev_dbg(&d->pdev->dev, "ENC: skipping frame, encoder not running\n");
        return 0;
    }

    /*
     * Build frame info structure.
     *
     * For H.264 (frame_type == 3), the address layout from decompile:
     *   - For (ChipType & 0xe) != 0 (our case):
     *     card_addr = p2 (direct)
     *     chroma_addr = p3 + ((p4 + 0x3ff) >> 10) * 0x100
     *   - Size is p4 (param_7)
     *   - PTS is p5 & 0x7FFFFFFF
     *   - Keyframe is (p5 & 0x80000000) != 0
     */
    memset(&frame, 0, sizeof(frame));

    frame.card_addr = p2;
    frame.size = p4;
    frame.pts = p5 & 0x7FFFFFFF;
    frame.keyframe = (p5 & 0x80000000) ? 1 : 0;
    frame.is_video = (cmd == 0x40) ? 1 : 0;
    frame.data_type = data_type;
    frame.frame_type = frame_type;
    frame.sequence = d->frame_sequence++;

    /* Secondary address calculation for H.264 */
    if (frame_type == FRAME_TYPE_COMPRESSED) {
        frame.chroma_addr = p3 + ((p4 + 0x3FF) >> 10) * 0x100;
    } else {
        frame.chroma_addr = p3;
    }

    /* Extra flags from p1 upper bytes */
    frame.audio_type = (p1 >> 16) & 0xFF;
    frame.extra_flags = (p1 >> 24) & 0xFF;

    dev_dbg(&d->pdev->dev,
            "ENC: frame seq=%u addr=0x%08x size=%u pts=%u key=%d\n",
            frame.sequence, frame.card_addr, frame.size,
            frame.pts, frame.keyframe);

    /*
     * Queue frame info to FIFO.
     * The DMA processing will dequeue and transfer.
     */
    spin_lock_irqsave(&d->frame_lock, flags);

    if (kfifo_avail(&d->frame_fifo) < sizeof(frame)) {
        spin_unlock_irqrestore(&d->frame_lock, flags);
        dev_warn(&d->pdev->dev, "ENC: frame FIFO full, dropping frame\n");
        return -ENOSPC;
    }

    kfifo_in(&d->frame_fifo, (u8 *)&frame, sizeof(frame));

    spin_unlock_irqrestore(&d->frame_lock, flags);

    /*
     * Trigger DMA processing.
     * This can be done via tasklet, workqueue, or direct call.
     * For now, schedule work to process in non-IRQ context.
     */
    schedule_work(&d->frame_work);

    return 0;
}

/**
 * encoder_get_pending_frame - Dequeue next frame from FIFO
 * @d: device context
 * @frame: output frame info structure
 *
 * Returns 0 if frame dequeued, -ENOENT if FIFO empty.
 */
int encoder_get_pending_frame(struct c985_poc *d, struct frame_info *frame)
{
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&d->frame_lock, flags);

    if (kfifo_len(&d->frame_fifo) < sizeof(*frame)) {
        spin_unlock_irqrestore(&d->frame_lock, flags);
        return -ENOENT;
    }

    ret = kfifo_out(&d->frame_fifo, (u8 *)frame, sizeof(*frame));

    spin_unlock_irqrestore(&d->frame_lock, flags);

    if (ret != sizeof(*frame))
        return -EIO;

    return 0;
}

/**
 * encoder_frames_pending - Check if frames are waiting
 * @d: device context
 *
 * Returns number of pending frames.
 */
unsigned int encoder_frames_pending(struct c985_poc *d)
{
    return kfifo_len(&d->frame_fifo) / sizeof(struct frame_info);
}

/**
 * encoder_set_running - Set encoder running state
 * @d: device context
 * @running: true if encoder is producing frames
 */
void encoder_set_running(struct c985_poc *d, bool running)
{
    d->encoder_running = running;

    if (running) {
        d->frame_sequence = 0;
        kfifo_reset(&d->frame_fifo);
    }

    dev_info(&d->pdev->dev, "Encoder %s\n", running ? "started" : "stopped");
}

