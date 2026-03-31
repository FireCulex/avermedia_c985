/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AVERMEDIA_C985_H
#define AVERMEDIA_C985_H

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

struct c985_buffer {
    struct vb2_v4l2_buffer vb;
    struct list_head list;
};

struct c985_poc {
    struct pci_dev *pdev;
    void __iomem   *bar1;
    u32             chip_ver;

    /* IRQ */
    int             irq_registered;

    /* Encoder register offsets */
    u32             enc_reg_message;
    u32             enc_reg_system_control;
    u32             enc_reg_picture_resolution;
    u32             enc_reg_input_control;
    u32             enc_reg_rate_control;
    u32             enc_reg_bit_rate;
    u32             enc_reg_filter_control;
    u32             enc_reg_gop_loop_filter;
    u32             enc_reg_out_pic_resolution;
    u32             enc_reg_et_control;
    u32             enc_reg_block_size;
    u32             enc_reg_audio_control_param;
    u32             enc_reg_audio_control_ex;

    /* Video output settings */
    u32             vo_enable;
    u32             vou_mode;
    u32             vou_start_pixel;
    u32             vou_start_line;

    /* Audio output settings */
    u32             ao_enable;
    u32             ao_controls;
    u32             aud_controls;
    u32             ai_volume;
    u32             ao_volume;
    u8 mcu_addr;           /* NUC100 I2C address (7-bit) */
    u8 mcu_rst_gpio;
    u8 aud_switch_gpio1;
    u8 aud_switch_gpio2;

    /* V4L2 */
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
};

/* BAR indices */
#define C985_BAR_MMIO       1

/* Registers (BAR1 offsets) */
#define REG_ARM_CTRL        0x0000
#define REG_CLOCK_GATE      0x0010
#define REG_WATCHDOG_SET    0x0018
#define REG_TIMESTAMP       0x001c
#define REG_CHIP_VER        0x0038
#define REG_CAPCTL_BASE     0x0040
#define REG_AO_VO_CTL       0x0050
#define REG_GPIO_DIR        0x0610
#define REG_GPIO_VAL        0x0614
#define REG_GPIO_IN         0x0618
#define REG_CLK_POWER       0x06cc
#define REG_CPR_RD_ADDR     0x0780
#define REG_CPR_RD_CTL      0x0784
#define REG_CPR_RD_DATA     0x0788
#define REG_CPR_WR_ADDR     0x078c
#define REG_CPR_WR_CTL      0x0790
#define REG_CPR_WR_DATA     0x0794
#define REG_ARM_RESET       0x0800
#define REG_ARM_BOOT        0x080c
#define REG_MEM_WIN_BASE    0x081c
#define REG_MEM_CTL         0x0840

/* Bits */
#define AO_ENABLE_BIT       BIT(1)
#define VO_ENABLE_BIT       BIT(2)

/* Firmware layout */
#define CARD_RAM_VIDEO_BASE 0x00000000
#define CARD_RAM_AUDIO_BASE 0x00100000

/* PLL defaults */
#define PLL4_REG            0x200
#define PLL4_VAL_10020      0x00030130
#define PLL5_REG            0x00cc
#define PLL5_VAL_DEFAULT    0x00010239

/* Firmware files */
#define FW_VIDEO            "avermedia/qpvidfwpcie.bin"
#define FW_AUDIO            "avermedia/qpaudfw.bin"

/* CPR */
#define CPR_CHIPVER_SPECIAL 0x10
#define CPR_TIMEOUT_MS      3000

/* QPHCI */
#define QPHCI_PAGE_SIZE     0x100000

/* QSOS */
#define FW_MAGIC            0x5351534f

#endif
