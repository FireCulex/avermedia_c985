/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ENCODER_H
#define ENCODER_H

#include <linux/types.h>
#include <linux/kfifo.h>

struct c985_poc;

/* FIFO size in number of frame_info entries */
#define ENCODER_FIFO_SIZE   32

/**
 * struct frame_info - Encoded frame metadata from ARM
 * @card_addr:    Card RAM address of frame data (multiply by 4 if needed)
 * @chroma_addr:  Secondary address (for YUV chroma plane)
 * @size:         Frame size in bytes
 * @pts:          Presentation timestamp (90kHz clock typically)
 * @keyframe:     1 if this is a keyframe (IDR for H.264)
 * @is_video:     1 for video, 0 for audio
 * @data_type:    Original data type code from ARM
 * @frame_type:   Derived frame type (1=YUV planar, 2=YUV packed, 3=compressed, 4=other)
 * @audio_type:   Audio codec type (from p1 bits 23:16)
 * @extra_flags:  Extra flags (from p1 bits 31:24)
 * @sequence:     Host-side sequence number
 */
struct frame_info {
    u32 card_addr;
    u32 chroma_addr;
    u32 size;
    u32 pts;
    u8  keyframe;
    u8  is_video;
    u16 data_type;
    u8  frame_type;
    u8  audio_type;
    u8  extra_flags;
    u8  reserved;
    u32 sequence;
};

/* Function prototypes */
int encoder_init(struct c985_poc *d);
void encoder_cleanup(struct c985_poc *d);

int encoder_parse_arm_message(struct c985_poc *d,
                              u8 cmd, u32 p1, u32 p2, u32 p3, u32 p4, u32 p5);

int encoder_get_pending_frame(struct c985_poc *d, struct frame_info *frame);
unsigned int encoder_frames_pending(struct c985_poc *d);

void encoder_set_running(struct c985_poc *d, bool running);

#endif /* ENCODER_H */
