/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_TYPES_H
#define C985_TYPES_H

#include <linux/types.h>

/* ============================================
 * Error Codes (_EQPErrors)
 * ============================================ */
#define QPERR_SUCCESS       0
#define QPERR_PENDING       1
#define QPERR_CTRL_VERSION  245
#define QPERR_AGAIN         246
#define QPERR_NOTFOUND      247
#define QPERR_CANCELLED     248
#define QPERR_INVALID       249
#define QPERR_TIMEOUT       250
#define QPERR_NOTSUPP       251
#define QPERR_NOTIMPL       252
#define QPERR_PARMS         253
#define QPERR_NOMEM         254
#define QPERR_FAIL          255

/* ============================================
 * Forward Declarations
 * ============================================ */
struct c985_poc;
struct cql_codec;
struct cql_codec_lib;
struct ihciapi;
struct c_task;
struct c_queue;
struct c_channel;
struct c_fifo;
struct task_data;
struct pcie_device_extension;

/* ============================================
 * GPIO Direction Enum
 * ============================================ */
enum gpio_direction {
    GPIO_DIR_INPUT = 0,
    GPIO_DIR_OUTPUT = 1,
};

/* ============================================
 * ARM Buffer Type
 * ============================================ */
enum arm_buffer_type {
    ARM_BUF_INVALID = 0,
    ARM_BUF_YUV = 1,
    ARM_BUF_YUVMB2RAS = 2,
    ARM_BUF_YUVRAS = 3,
    ARM_BUF_OTHERS = 4,
};

/* ============================================
 * Task State
 * ============================================ */
enum task_state {
    TASK_STATE_IDLE = 0,
    TASK_STATE_RUN = 1,
    TASK_STATE_STOP = 2,
};


/* ============================================
 * Task Reply Events (bitmask)
 * ============================================ */
#define TASK_REPLY_EVENT_STOP       0x10
#define TASK_REPLY_EVENT_PLAYINFO   0x20

/* ============================================
 * Task Types
 * ============================================ */
enum task_type {
    TASK_TYPE_ENC = 0,
    TASK_TYPE_DEC = 1,
    TASK_TYPE_END = 2,
};

/* ============================================
 * QP State
 * ============================================ */
enum qp_state {
    QPSTATE_STOP = 0,
    QPSTATE_ACQUIRE = 1,
    QPSTATE_PAUSE = 2,
    QPSTATE_RUN = 3,
};

/* ============================================
 * Task/Channel Data Types
 * ============================================ */
enum task_data_type {
    TASK_DATA_TYPE_COMP_VID = 0,
    TASK_DATA_TYPE_COMP_AUD = 1,
    TASK_DATA_TYPE_INDEX = 2,
    TASK_DATA_TYPE_VBI = 3,
    TASK_DATA_TYPE_YUV = 4,
    TASK_DATA_TYPE_PCM = 5,
    TASK_DATA_TYPE_MAX = 7,
    TASK_DATA_TYPE_VIRTUAL = 6,
    TASK_DATA_TYPE_END = 7,
    TASK_DATA_TYPE_INVALID = 0xFFFFFFFF,
};

/* ============================================
 * Channel Direction
 * ============================================ */
enum channel_direction {
    CHANNEL_DIR_READ = 0,
    CHANNEL_DIR_WRITE = 1,
    CHANNEL_DIR_NONE = 2,
};


/* ============================================
 * HCI Mode / Bus Type
 * ============================================ */
enum qphci_mode {
    QPHCI_MODE_DIRECT = 0,
    QPHCI_MODE_INDIRECT = 1,
};

enum qphci_bus {
    QPHCI_BUS_USB = 0,
    QPHCI_BUS_PCI = 1,
    QPHCI_BUS_CUSTOM_1 = 2,
    QPHCI_BUS_201_EMULATION = 3,
    QPHCI_BUS_INTERNAL = 4,
};

enum qpi2c_type {
    QPI2C_TYPE_DUMMY = 0,
    QPI2C_TYPE_USE_USB = 1,
    QPI2C_TYPE_USE_QLCODEC = 2,
    QPI2C_TYPE_USE_201_EMULATION = 3,
    QPI2C_TYPE_SOC_INTERNAL_HW = 4,
};

/* ============================================
 * DMA Direction
 * ============================================ */
enum dma_dir {
    DMA_DIR_READ = 0,
    DMA_DIR_WRITE = 1,
};

/* ============================================
 * KSPIN Communication Types
 * ============================================ */
#define KSPIN_COMMUNICATION_NONE        0
#define KSPIN_COMMUNICATION_SINK        1
#define KSPIN_COMMUNICATION_BOTH        2
#define KSPIN_COMMUNICATION_BRIDGE      3
#define KSPIN_COMMUNICATION_DATASINK    4

/* ============================================
 * KSPIN Data Flow
 * ============================================ */
#define KSPIN_DATAFLOW_IN               0
#define KSPIN_DATAFLOW_OUT              1

/* ============================================
 * Project Input Control
 * ============================================ */
enum project_input_control {
    PROJECT_VID_HDMI_INPUT = 0,
    PROJECT_VID_RGB_INPUT = 1,
    PROJECT_AUD_EXTERNAL_INPUT_JACK = 2,
    PROJECT_AUD_HDMI_INPUT = 3,
    PROJECT_AUD_EXTERNAL_INPUT_LR = 4,
    PROJECT_VID_COMP_INPUT = 5,
};

/* ============================================
 * C985 Video Input Source
 *
 * Identifies which chip provides video input:
 *   6604 = SiI6604 HDMI receiver (digital HDMI)
 *   7002 = ADV7002 (analog component/composite)
 * ============================================ */
enum c985_video_input {
    C985_VIDEO_INPUT_6604 = 0,      /* SiI6604 - HDMI */
    C985_VIDEO_INPUT_7002 = 1,      /* ADV7002 - Component/Analog */
};

/* ============================================
 * C985 Audio Input Source
 *
 * Identifies which chip provides audio input:
 *   6604 = SiI6604 HDMI receiver (embedded HDMI audio)
 *   3101 = TI3101 (external audio jack / analog)
 * ============================================ */
enum c985_audio_input {
    C985_AUDIO_INPUT_6604 = 0,      /* SiI6604 - HDMI embedded audio */
    C985_AUDIO_INPUT_3101 = 1,      /* TI3101 - External audio jack */
};

/* -----------------------------------------------------------------------
 * Firmware Data Types - ARM/Firmware channel type identifiers
 * --------------------------------------------------------------------- */

enum qp_fw_data_type {
    FW_DATA_TYPE_RAW_VID_INPUT        = 0x01,
    FW_DATA_TYPE_RAW_AUD_INPUT        = 0x02,
    FW_DATA_TYPE_CMPR_SYS_VID_INPUT   = 0x03,
    FW_DATA_TYPE_CMPR_AUD_INPUT       = 0x04,
    FW_DATA_TYPE_INDEX_INPUT          = 0x05,
    FW_DATA_TYPE_VBI_INPUT            = 0x06,
    FW_DATA_TYPE_RAW_YUYV_INPUT       = 0x07,

    FW_DATA_TYPE_RAW_VID_OUTPUT       = 0x81,
    FW_DATA_TYPE_RAW_AUD_OUTPUT       = 0x82,
    FW_DATA_TYPE_CMPR_SYS_VID_OUTPUT  = 0x83,
    FW_DATA_TYPE_CMPR_AUD_OUTPUT      = 0x84,
    FW_DATA_TYPE_INDEX_OUTPUT         = 0x85,
    FW_DATA_TYPE_VBI_OUTPUT           = 0x86,
    FW_DATA_TYPE_RAW_YUYV_OUTPUT      = 0x87,
};

/* ============================================
 * GPIO Structures (packed)
 * ============================================ */

/* _GPIO_BIT_DIRECTION (5 bytes packed) */
struct gpio_bit_direction {
    u8 bBitNumber;
    enum gpio_direction Direction;
} __packed;

/* _GPIO_BIT_VALUE (5 bytes packed) */
struct gpio_bit_value {
    u8 bBitNumber;
    int bValue;
} __packed;

/* ============================================
 * ARM Message (to ARM) - 4 bytes
 * ============================================ */
struct arm_message {
    union {
        u32 Read;
        struct {
            u16 command;
            u16 reserved;
        } Reg;
        struct {
            u16 command;
            u16 channel_number;
        } RegArtesa;
    };
};

/* ============================================
 * Host Message (from ARM) - 4 bytes
 * ============================================ */
struct host_message {
    union {
        u32 Read;
        struct {
            u16 command;
            u16 reserved;
        } Reg;
        struct {
            u16 command;
            u16 channel_number;
        } RegArtesa;
    };
};

/* ============================================
 * Host Message Status - 4 bytes
 * ============================================ */
struct host_message_status {
    union {
        u32 Read;
        struct {
            u8 status : 1;
            u8 reserved1 : 7;
            u8 rply : 1;
            u8 reserved2 : 7;
            u16 msg_id;
        } Reg;
        struct {
            u8 status : 1;
            u8 reserved1 : 7;
            u8 rply : 1;
            u8 reserved2 : 7;
            u16 channel_number;
        } RegArtesa;
    };
};

/* ============================================
 * HDMI Info Structure (32 bytes)
 * ============================================ */
struct hdmi_info {
    u16 HActive;            /* 0x00 */
    u16 VActive;            /* 0x02 */
    u16 HTotal;             /* 0x04 */
    u16 VTotal;             /* 0x06 */
    s32 PCLK;               /* 0x08 - Pixel clock in kHz */
    u8 xCnt;                /* 0x0C */
    u8 ScanMode;            /* 0x0D - 0=progressive, 1=interlaced */
    u8 VPolarity;           /* 0x0E */
    u8 HPolarity;           /* 0x0F */
    u16 Rate;               /* 0x10 - Frame rate * 100 */
    u8 _pad1[2];            /* 0x12 */
    u32 QP_InCtrl;          /* 0x14 - Encoder input control reg */
    u32 QP_InRes;           /* 0x18 - Encoder input resolution reg */
    u32 QP_InSync;          /* 0x1C - Encoder sync mode reg */
};

/* ============================================
 * Hardware Config Structure (36 bytes)
 * ============================================ */
struct hw_config {
    u32 PnPid;                  /* 0x00 */
    u8 mcu_addr;                /* 0x04 - NUC100 I2C address */
    u8 mcu_rst_gpio;            /* 0x05 - NUC100 reset GPIO */
    u8 dgtl_vid_addr;           /* 0x06 - Digital video chip I2C addr */
    u8 dgtl_vid_rst_gpio;       /* 0x07 */
    u8 anlg_vid_addr;           /* 0x08 - Analog video chip I2C addr */
    u8 anlg_vid_rst_gpio;       /* 0x09 */
    u8 dgtl_aud_addr;           /* 0x0A - Digital audio chip I2C addr */
    u8 dgtl_aud_rst_gpio;       /* 0x0B */
    u8 anlg_aud_addr;           /* 0x0C - Analog audio chip I2C addr */
    u8 anlg_aud_rst_gpio;       /* 0x0D */
    u8 vid_switch_gpio1;        /* 0x0E */
    u8 vid_switch_gpio2;        /* 0x0F */
    u8 aud_switch_gpio1;        /* 0x10 */
    u8 aud_switch_gpio2;        /* 0x11 */
    u8 _pad1[2];                /* 0x12 */
    u32 BusNumber;              /* 0x14 */
    int is_component_input;     /* 0x18 */
    int is_RGB_input;           /* 0x1C */
    int is_external_audio_input;/* 0x20 */
};

/* ============================================
 * Task DMA Request (28 bytes, packed)
 * ============================================ */
struct task_dma_request {
    u32 ulArmAddr;              /* 0x00 */
    u8 *pBuffer;                /* 0x04 */
    u32 ulLength;               /* 0x0C */
    u32 ulOffset;               /* 0x10 */
    enum channel_direction dir; /* 0x14 */
    int bSwap;                  /* 0x18 */
} __packed;

/* ============================================
 * PCI R/W RAM Structure (57 bytes, packed)
 * ============================================ */
struct pci_rw_ram_struct {
    u32 bar;                    /* 0x00 */
    u16 transferSize;           /* 0x04 */
    u16 repeatCount;            /* 0x06 */
    u64 offset;                 /* 0x08 */
    u64 data;                   /* 0x10 */
    u64 transferTime;           /* 0x18 */
    u64 driverTime;             /* 0x20 */
    u32 TransferMode;           /* 0x28 */
    u32 PicWidthMB;             /* 0x2C */
    u32 PicHeightMB;            /* 0x30 */
    u32 DataSwap;               /* 0x34 */
    u8 IsFrameMode;             /* 0x38 */
} __packed;

#endif /* C985_TYPES_H */
