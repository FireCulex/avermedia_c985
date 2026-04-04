/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AVERMEDIA_C985_H
#define AVERMEDIA_C985_H

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

#include <linux/kfifo.h>
#include <linux/workqueue.h>

struct c985_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

struct c985_poc {
    /* ============================================
     * PCI / Hardware
     * ============================================ */
    struct pci_dev *pdev;
    void __iomem   *bar0;           /* BAR0 = DMA engines */
    void __iomem   *bar1;           /* BAR1 = registers */
    u32             chip_ver;
    int             irq_registered;

    /* ============================================
     * Firmware State
     * ============================================ */
    u32 qpsos_version;
    u32 config_base;

    /* ============================================
     * Platform Configuration (from Windows Registry)
     * ============================================ */

    /* Core chip settings */
    u32 power_state;
    u32 chip_type;              /* Windows: 8 */
    u32 access_mode;            /* Windows: 1 */
    u32 bus_type;               /* Windows: 1 (PCIe) */
    u32 mem_type;               /* Windows: 1 */
    u32 mem_size;               /* Windows: 0x200 */
    u32 ver_fw_api;             /* Windows: 1 */
    u32 fw_fixed_mode;          /* Windows: 1 */
    u32 fw_int_mode;            /* Windows: 0 */
    u32 error_recovery;         /* Windows: 2 */

    /* Video Input Unit (VIU) */
    u32 vid_input_type;         /* Windows: 0x0A (HDMI) */
    u32 vid_input_channel;      /* Windows: 0x14 */
    u32 viu_mode;               /* Windows: 2 */
    u32 viu_format;             /* Windows: 0 */
    u32 viu_start_pixel;        /* Windows: 0 */
    u32 viu_start_line;         /* Windows: 0 */
    u32 viu_clk_edge;           /* Windows: 0 */
    u32 viu_sync_code1;         /* Windows: 0xF1F1F1DA */
    u32 viu_sync_code2;         /* Windows: 0xB6F1F1B6 */

    /* Audio Input */
    u32 ai_msb;                 /* Windows: 1 */
    u32 ai_lrclk;               /* Windows: 1 */
    u32 ai_bclk;                /* Windows: 0 */
    u32 ai_i2s;                 /* Windows: 1 */
    u32 ai_rj;                  /* Windows: 0 */
    u32 ai_m;                   /* Windows: 0 */
    u32 ai_volume;              /* Windows: 8 */
    u32 aud_controls;           /* Combined AI control word */

    /* Video Output */
    u32 vo_enable;              /* Windows: 0 */
    u32 vou_mode;               /* Windows: 0 */
    u32 vou_start_pixel;        /* Windows: 1 */
    u32 vou_start_line;         /* Windows: 0 */

    /* Audio Output */
    u32 ao_enable;              /* Windows: 1 */
    u32 ao_msb;                 /* Windows: 1 */
    u32 ao_lrclk;               /* Windows: 1 */
    u32 ao_bclk;                /* Windows: 0 */
    u32 ao_i2s;                 /* Windows: 1 */
    u32 ao_rj;                  /* Windows: 1 */
    u32 ao_s;                   /* Windows: 0 */
    u32 ao_volume;              /* Windows: 8 */
    u32 ao_controls;            /* Combined AO control word */

    /* GPIO */
    u32 gpio_directions;        /* Register 0x610 */
    u32 gpio_values;            /* Register 0x614 */

    /* I2C Configuration */
    u32 i2c_type;               /* Windows: 2 */
    u32 i2c_gpio_clk;           /* Windows: 0x0E (14) */
    u32 i2c_gpio_data;          /* Windows: 0x0F (15) */
    u32 i2c_ex_type;            /* Windows: 5 */

    /* C985-Specific Hardware */
    u8 mcu_addr;                /* Windows: 0x2B (NUC100) */
    u8 mcu_rst_gpio;            /* Windows: 5 */
    u8 alg_aud_addr;            /* Windows: 0x30 (TI3101) */
    u8 alg_aud_rst_gpio;        /* Windows: 6 */
    u8 aud_switch_gpio1;        /* Windows: 0x0D (13) */
    u8 aud_switch_gpio2;        /* Windows: 7 */

    /* PLL overrides */
    u32 pll4_override;          /* 0 = use default */
    u32 pll5_override;          /* 0 = use default */

    /* ============================================
     * Encoder Configuration
     * ============================================ */

    /* Encoder settings (from Windows [Encoder] registry) */
    u32 enc_function;               /* Windows: 0x80000011 */
    u32 enc_system_control;         /* Windows: 0x2101B219 */
    u32 enc_rate_control;           /* Windows: 0x005003E8 */
    u32 enc_rate_control_ex;        /* Windows: 0x00014050 */
    u32 enc_gop_loop_filter;        /* Windows: 0xF199003C */
    u32 enc_picture_resolution;     /* Windows: 0x02D00500 */
    u32 enc_out_pic_resolution;     /* Windows: 0x02D00500 */
    u32 enc_input_control;          /* Windows: 0x0F7C0609 */
    u32 enc_sync_mode;              /* Windows: 0x00000011 */
    u32 enc_bit_rate;               /* Windows: 0x1F4007D0 */
    u32 enc_filter_control;         /* Windows: 0x80002000 */
    u32 enc_et_control;             /* Windows: 0 */
    u32 enc_block_size;             /* Windows: 0x10 */
    u32 enc_stop_mode;              /* Windows: 0 */
    u32 enc_enable_vid_padding;     /* Windows: 1 */

    /* Encoder links */
    u32 enc_link_vin;               /* Windows: 0x08 */
    u32 enc_link_vout;              /* Windows: 0x01 */
    u32 enc_link_ain;               /* Windows: 0x08 */
    u32 enc_link_aout;              /* Windows: 0x01 */

    /* Audio encoding */
    u32 enc_audio_control_param;    /* Windows: 0x21121080 */
    u32 enc_audio_control_ex_aac;   /* Windows: 0x520800F2 */
    u32 enc_audio_control_ex_g711;  /* Windows: 0 */
    u32 enc_audio_control_ex_lpcm;  /* Windows: 0x00000200 */
    u32 enc_audio_control_ex_silk;  /* Windows: 0x02 */

    /* Advanced encoder */
    u32 enc_large_compress_buf_ctrl;/* Windows: 0x80004A38 */
    u32 enc_mjpeg_quality;          /* Windows: 0x0B */
    u32 enc_mjpeg_frame_buffer;     /* Windows: 0 */
    u32 enc_index_cap_freq;         /* Windows: 0x20 */
    u32 enc_mp4_video_block_number; /* Windows: 5 */

    /* Decimation/scaling */
    u32 enc_decimation_input_format;  /* Windows: 0 */
    u32 enc_decimation_output_format; /* Windows: 1 */
    u32 enc_decimation_scale_factor;  /* Windows: 0x01040104 */
    u32 enc_deinterlace_mode;         /* Windows: 1 */

    /* Encoder register offsets */
    u32 enc_reg_message;
    u32 enc_reg_system_control;
    u32 enc_reg_picture_resolution;
    u32 enc_reg_input_control;
    u32 enc_reg_rate_control;
    u32 enc_reg_bit_rate;
    u32 enc_reg_filter_control;
    u32 enc_reg_gop_loop_filter;
    u32 enc_reg_out_pic_resolution;
    u32 enc_reg_et_control;
    u32 enc_reg_block_size;
    u32 enc_reg_audio_control_param;
    u32 enc_reg_audio_control_ex;

    /* ============================================
     * V4L2 / Streaming
     * ============================================ */
    struct v4l2_device v4l2_dev;
    struct video_device vdev;
    struct vb2_queue vb2_queue;
    struct mutex v4l2_lock;
    struct list_head buf_list;
    spinlock_t buf_lock;
    unsigned int width;
    unsigned int height;
    unsigned int sequence;
    int hdmi_signal_cached;

    /* ============================================
     * Frame Handling
     * ============================================ */
    DECLARE_KFIFO_PTR(frame_fifo, u8);
    spinlock_t frame_lock;
    struct list_head pending_frames;
    struct work_struct frame_work;
    u32 frame_sequence;
    bool encoder_running;

    /* ============================================
     * IRQ Handling
     * ============================================ */
    spinlock_t irq_lock;
    struct work_struct irq_work;
    struct work_struct dma_work;
    struct completion mailbox_complete;

    /* ============================================
     * DMA State
     * ============================================ */
    spinlock_t dma_lock;
    struct completion dma_done;
    bool dma_active;
    dma_addr_t current_dma_addr;
    u32 current_dma_len;
    int num_dma_channels;
    u32 dma_engine_idx[8];
    u32 dma_interrupt_status;
};

#define DRV_NAME "avermedia_c985_poc"
#define DRV_DESC "AVerMedia Live Gamer HD Series (C985)"

/* ============================================
 * BAR Indices
 * ============================================ */
#define C985_BAR_DMA        0
#define C985_BAR_MMIO       1

/* ============================================
 * Register Offsets (BAR1)
 * ============================================ */
#define REG_CLOCK_GATE      0x0010
#define REG_WATCHDOG_SET    0x0018
#define REG_TIMESTAMP       0x001C
#define REG_CHIP_VER        0x0038
#define REG_CAPCTL_BASE     0x0040
#define REG_AO_VO_CTL       0x0050
#define REG_GPIO_DIR        0x0610
#define REG_GPIO_VAL        0x0614
#define REG_GPIO_IN         0x0618
#define REG_CLK_POWER       0x06CC
#define REG_CPR_RD_ADDR     0x0780
#define REG_CPR_RD_CTL      0x0784
#define REG_CPR_RD_DATA     0x0788
#define REG_CPR_WR_ADDR     0x078C
#define REG_CPR_WR_CTL      0x0790
#define REG_CPR_WR_DATA     0x0794

/* ============================================
 * Register Bits
 * ============================================ */
#define AO_ENABLE_BIT       BIT(1)
#define VO_ENABLE_BIT       BIT(2)

/* ============================================
 * Firmware Layout
 * ============================================ */
#define CARD_RAM_VIDEO_BASE 0x00000000
#define CARD_RAM_AUDIO_BASE 0x00100000

/* ============================================
 * PLL Defaults
 * ============================================ */
#define PLL4_REG            0x00C8
#define PLL4_VAL_10020      0x00030130
#define PLL4_VAL_DEFAULT    0x00020236
#define PLL5_REG            0x00CC
#define PLL5_VAL_DEFAULT    0x00010239

/* ============================================
 * Firmware Files
 * ============================================ */
#define FW_VIDEO            "avermedia/qpvidfwpcie.bin"
#define FW_AUDIO            "avermedia/qpaudfw.bin"

/* ============================================
 * CPR Constants
 * ============================================ */
#define CPR_CHIPVER_SPECIAL 0x10
#define CPR_TIMEOUT_MS      3000

/* ============================================
 * QPHCI Constants
 * ============================================ */
#define QPHCI_PAGE_SIZE     0x100000

/* ============================================
 * QPSOS Constants
 * ============================================ */
#define FW_MAGIC            0x5351534F  /* "QSOS" */

#endif /* AVERMEDIA_C985_H */

void c985_get_init_data(struct c985_poc *d);

