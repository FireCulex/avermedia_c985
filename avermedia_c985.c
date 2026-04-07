// SPDX-License-Identifier: GPL-2.0
// avermedia_c985.c — PCI driver wrapper for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "avermedia_c985.h"
#include "structs.h"
#include "cqlcodec.h"
#include "ti3101.h"
#include "nuc100.h"
#include "project.h"
#include "v4l2.h"

MODULE_DESCRIPTION(DRV_DESC);

static int c985_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;
    int i;

    /* Fix PCI class code for proper device identification */
    pdev->class = PCI_CLASS_MULTIMEDIA_VIDEO << 8;

    ret = cqlcodec_init_device(pdev, id);
    if (ret)
        return ret;

    d = pci_get_drvdata(pdev);

    /* Scan for available DMA channels (from PedDmaInit) */
    d->pcie.m_NumDmaAvailable = 0;
    for (i = 0; i < 64; i++) {
        u32 caps = readl(c985_bar0(d) + (i * 0x100));  /* PED_DMA_ENGINE.Capabilities */
        if (caps & 0x01) {
            d->pcie.m_NumDmaAvailable++;
        }
    }
    dev_info(&pdev->dev, "Found %d DMA channels\n", d->pcie.m_NumDmaAvailable);

    ret = cqlcodec_fw_download(d, 1);
    if (ret)
        goto err_remove;

    ret = project_c985_init(d);
    if (ret)
        goto err_remove;

    /* Register V4L2 */
    /*    ret = c985_v4l2_register(d);
     *   if (ret)
     *       goto err_remove; */

    return 0;

    err_remove:
    cqlcodec_remove_device(pdev);
    return ret;
}

static void c985_pci_remove(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);

    if (d) {
        /* c985_v4l2_unregister(d); */
    }
    cqlcodec_remove_device(pdev);
}

static const struct pci_device_id c985_pci_ids[] = {
    { PCI_DEVICE(0x1af2, 0xa001) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, c985_pci_ids);

static struct pci_driver c985_pci_driver = {
    .name     = DRV_NAME,
    .id_table = c985_pci_ids,
    .probe    = c985_pci_probe,
    .remove   = c985_pci_remove,
};

module_pci_driver(c985_pci_driver);

MODULE_DESCRIPTION("AVerMedia C985 PoC driver");
MODULE_AUTHOR("fireculex@gmail.com");
MODULE_LICENSE("GPL");

void CDEVICE__getInitData(struct c985_poc *d)
{
    /*
     * ========================================
     * Core Platform Configuration (into codec)
     * ========================================
     */
    d->codec.m_ChipType = 8;              /* QPPF_MODE_DIRECT */
    d->codec.m_hci.m_access_mode = QPHCI_MODE_DIRECT;
    d->codec.m_hci.m_bus_type = QPHCI_BUS_PCI;
    d->codec.m_MemType = 1;
    d->codec.m_MemSize = 0x200;           /* 512KB */
    d->codec.m_VerFwAPI = 1;
    d->codec.m_FwFixedMode = 1;
    d->codec.m_FwIntMode = 0;             /* polling mode */
    d->codec.m_ErrorRecovery = 2;

    /*
     * ========================================
     * Video Input Unit (VIU) Configuration
     * ========================================
     */
    d->m_VidInputType = 0x0A;             /* HDMI digital input */
    d->m_VidInputChannel = 0x14;
    d->codec.m_VIUMode = 2;
    d->codec.m_VIUFormat = 0;
    d->codec.m_VIUStartPixel = 0;
    d->codec.m_VIUStartLine = 0;
    d->codec.m_ClkEdge = 0;
    d->codec.m_ulViuSyncCode1 = 0xF1F1F1DA;
    d->codec.m_ulViuSyncCode2 = 0xB6F1F1B6;

    /*
     * ========================================
     * Audio Input Configuration
     * ========================================
     */
    d->codec.m_AIControls_ai_msb = 1;
    d->codec.m_AIControls_lrclk_i = 1;
    d->codec.m_AIControls_bclk_i = 0;
    d->codec.m_AIControls_ai_i2s = 1;
    d->codec.m_AIControls_ai_rj = 0;
    d->codec.m_AIControls_ai_m = 0;

    /* Build combined audio input control word */
    d->codec.m_AudControls =
    (d->codec.m_AIControls_ai_msb << 0) |
    (d->codec.m_AIControls_lrclk_i << 1) |
    (d->codec.m_AIControls_bclk_i << 2) |
    (d->codec.m_AIControls_ai_i2s << 3) |
    (d->codec.m_AIControls_ai_rj << 4) |
    (d->codec.m_AIControls_ai_m << 5);

    /*
     * ========================================
     * Video Output Configuration
     * ========================================
     */
    d->codec.m_VOEnable = 0;              /* disabled on C985 */
    d->codec.m_VOUMode = 0;
    d->codec.m_VOUStartPixel = 1;
    d->codec.m_VOUStartLine = 0;

    /*
     * ========================================
     * Audio Output Configuration
     * ========================================
     */
    d->codec.m_AOEnable = 1;
    d->codec.m_AOControls_ao_msb = 1;
    d->codec.m_AOControls_lrclk_i = 1;
    d->codec.m_AOControls_bclk_i = 0;
    d->codec.m_AOControls_ao_i2s = 1;
    d->codec.m_AOControls_ao_rj = 1;
    d->codec.m_AOControls_ao_s = 0;

    /* Build combined audio output control word */
    d->codec.m_AOControls =
    (d->codec.m_AOControls_ao_msb << 0) |
    (d->codec.m_AOControls_lrclk_i << 1) |
    (d->codec.m_AOControls_bclk_i << 2) |
    (d->codec.m_AOControls_ao_i2s << 3) |
    (d->codec.m_AOControls_ao_rj << 4) |
    (d->codec.m_AOControls_ao_s << 5);

    /*
     * ========================================
     * I2C Configuration
     * ========================================
     */
    d->m_I2cType = 2;                     /* GPIO-based I2C */
    d->m_I2cGpioClk = 0x0E;               /* GPIO 14 */
    d->m_I2cGpioData = 0x0F;              /* GPIO 15 */
    d->m_I2cExType = 5;

    /*
     * ========================================
     * C985-Specific Hardware Configuration
     * ========================================
     */
    d->m_McuAddr = 0x2B;                  /* NUC100 I2C address */
    d->m_McuRstGpio = 5;                  /* GPIO 5 for MCU reset */
    d->m_AlgAudAddr = 0x30;               /* TI3101 I2C address */
    d->m_AlgAudRstGpio = 6;               /* GPIO 6 for audio reset */
    d->m_AudSwitchGpio1 = 0x0D;           /* GPIO 13 */
    d->m_AudSwitchGpio2 = 7;              /* GPIO 7 */

    /*
     * ========================================
     * PLL Overrides
     * ========================================
     */
    d->codec.m_Pll4 = 0;
    d->codec.m_Pll5 = 0;

    /*
     * ========================================
     * Encoder Configuration
     * ========================================
     */
    d->m_EncFunction = 0x80000011;
    d->m_EncSystemControl = 0x2101B219;
    d->m_EncRateControl = 0x005003E8;
    d->m_EncRateControlEx = 0x00014050;
    d->m_EncGopLoopFilter = 0xF199003C;
    d->m_EncPictureResolution = 0x02D00500;    /* 1280x720 */
    d->m_EncOutPicResolution = 0x02D00500;
    d->m_EncInputControl = 0x0F7C0609;
    d->m_EncSyncMode = 0x00000011;
    d->m_EncBitRate = 0x1F4007D0;
    d->m_EncFilterControl = 0x80002000;
    d->m_EncEtControl = 0;
    d->m_EncBlockSize = 0x10;
    d->m_EncStopMode = 0;
    d->m_EncEnableVidPadding = 1;

    /* Encoder links */
    d->m_EncLinkVin = 0x08;
    d->m_EncLinkVout = 0x01;
    d->m_EncLinkAin = 0x08;
    d->m_EncLinkAout = 0x01;

    /* Audio encoding */
    d->m_EncAudioControlParam = 0x21121080;
    d->m_EncAudioControlExAac = 0x520800F2;
    d->m_EncAudioControlExG711 = 0;
    d->m_EncAudioControlExLpcm = 0x00000200;
    d->m_EncAudioControlExSilk = 0x02;

    /* Advanced encoder settings */
    d->m_EncLargeCompressBufCtrl = 0x80004A38;
    d->m_EncMjpegQuality = 0x0B;
    d->m_EncMjpegFrameBuffer = 0;
    d->m_EncIndexCapFreq = 0x20;
    d->m_EncMp4VideoBlockNumber = 5;
    d->m_EncDecimationInputFormat = 0;
    d->m_EncDecimationOutputFormat = 1;
    d->m_EncDecimationScaleFactor = 0x01040104;
    d->m_EncDeinterlaceMode = 1;

    dev_dbg(&d->pdev->dev, "Configuration initialized:\n");
    dev_dbg(&d->pdev->dev, "  ChipType=%d AccessMode=%d BusType=%d\n",
            d->codec.m_ChipType, d->codec.m_hci.m_access_mode,
            d->codec.m_hci.m_bus_type);
    dev_dbg(&d->pdev->dev, "  VerFwAPI=%d FwFixedMode=%d FwIntMode=%d\n",
            d->codec.m_VerFwAPI, d->codec.m_FwFixedMode, d->codec.m_FwIntMode);
    dev_dbg(&d->pdev->dev, "  VidInputType=0x%X VIUMode=%d\n",
            d->m_VidInputType, d->codec.m_VIUMode);
    dev_dbg(&d->pdev->dev, "  AOEnable=%d VOEnable=%d\n",
            d->codec.m_AOEnable, d->codec.m_VOEnable);
    dev_dbg(&d->pdev->dev, "  MCU=0x%02X@GPIO%d TI3101=0x%02X@GPIO%d\n",
            d->m_McuAddr, d->m_McuRstGpio,
            d->m_AlgAudAddr, d->m_AlgAudRstGpio);
}
