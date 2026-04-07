/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AVERMEDIA_C985_H
#define AVERMEDIA_C985_H

/* ============================================
 * Forward Declarations
 * ============================================ */
struct c985_poc;

    /* ============================================
     * Driver Constants
     * ============================================ */
    #define DRV_NAME "avermedia_c985"
    #define DRV_DESC "AVerMedia Live Gamer HD (C985)"

    #define C985_BAR_DMA        0
    #define C985_BAR_MMIO       1

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
    #define REG_ARM_RESET       0x0800
    #define REG_ARM_STATUS      0x0804
    #define REG_ARM_BOOT        0x080C
    #define REG_MEM_WIN_BASE    0x081C
    #define REG_MEM_CTL         0x0840

    #define AO_ENABLE_BIT       BIT(1)
    #define VO_ENABLE_BIT       BIT(2)

    #define CARD_RAM_VIDEO_BASE 0x00000000
    #define CARD_RAM_AUDIO_BASE 0x00100000
    #define FW_VIDEO            "avermedia/qpvidfwpcie.bin"
    #define FW_AUDIO            "avermedia/qpaudfw.bin"
    #define FW_MAGIC            0x5351534F

    #define PLL4_REG            0x00C8
    #define PLL4_VAL_10020      0x00030130
    #define PLL4_VAL_DEFAULT    0x00020236
    #define PLL5_REG            0x00CC
    #define PLL5_VAL_DEFAULT    0x00010239

    #define CPR_CHIPVER_SPECIAL 0x10
    #define CPR_TIMEOUT_MS      3000

    #define QPHCI_PAGE_SIZE     0x100000

    /* ============================================
     * Function Prototypes
     * ============================================ */
    void CDEVICE__getInitData(struct c985_poc *d);


#endif
