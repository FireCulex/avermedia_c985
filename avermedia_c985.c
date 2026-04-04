// SPDX-License-Identifier: GPL-2.0
// avermedia_c985.c — PCI driver wrapper for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>


#include "avermedia_c985.h"
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
    d->num_dma_channels = 0;
    for (i = 0; i < 64; i++) {
        u32 caps = readl(d->bar0 + (i * 0x100));  /* PED_DMA_ENGINE.Capabilities */
        if (caps & 0x01) {
            d->num_dma_channels++;
        }
    }
    dev_info(&pdev->dev, "Found %d DMA channels\n", d->num_dma_channels);

    ret = cqlcodec_fw_download(d, 1);
    if (ret)
        goto err_remove;

    ret = project_c985_init(d);
    if (ret)
        goto err_remove;

    /* Register V4L2 */
    ret = c985_v4l2_register(d);
    if (ret)
        goto err_remove;

    return 0;

    err_remove:
    cqlcodec_remove_device(pdev);
    return ret;
}

static void c985_pci_remove(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);

    if (d) {
        c985_v4l2_unregister(d);
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

void c985_get_init_data(struct c985_poc *d)
{
    /*
     * ========================================
     * Core Platform Configuration
     * ========================================
     */

    /* ChipType = 8 → QPPF_MODE_DIRECT (direct register access) */
    d->chip_type = 8;

    /* AccessMode = 1 (enhanced/direct mode, not legacy mode 0) */
    d->access_mode = 1;

    /* BusType = 1 (PCIe, not USB) */
    d->bus_type = 1;

    /* MemType = 1 */
    d->mem_type = 1;

    /* MemSize = 0x200 (512KB) */
    d->mem_size = 0x200;

    /* VerFwAPI = 1 (firmware API version 1) */
    d->ver_fw_api = 1;

    /* FwFixedMode = 1 */
    d->fw_fixed_mode = 1;

    /* FwIntMode = 0 (polling mode, not interrupt mode) */
    d->fw_int_mode = 0;

    /* ErrorRecovery = 2 */
    d->error_recovery = 2;

    /* DontInitHW = 0 (do initialize hardware) */
    /* DownloadFW = 1 (do download firmware) - implicit in our flow */

    /*
     * ========================================
     * Video Input Unit (VIU) Configuration
     * ========================================
     */

    /* VidInputType = 0x0A (HDMI digital input) */
    d->vid_input_type = 0x0A;

    /* VidInputChannel = 0x14 */
    d->vid_input_channel = 0x14;

    /* VIUMode = 2 */
    d->viu_mode = 2;

    /* VIUFormat = 0 */
    d->viu_format = 0;

    /* VIUStartPixel = 0 */
    d->viu_start_pixel = 0;

    /* VIUStartLine = 0 */
    d->viu_start_line = 0;

    /* VIUClkEdge = 0 */
    d->viu_clk_edge = 0;

    /* VIUSyncCode1 = 0xF1F1F1DA */
    d->viu_sync_code1 = 0xF1F1F1DA;

    /* VIUSyncCode2 = 0xB6F1F1B6 */
    d->viu_sync_code2 = 0xB6F1F1B6;

    /*
     * ========================================
     * Audio Input Configuration
     * ========================================
     */

    /* AI_msb = 1 (MSB first) */
    d->ai_msb = 1;

    /* AI_lrclk = 1 */
    d->ai_lrclk = 1;

    /* AI_bclk = 0 */
    d->ai_bclk = 0;

    /* AI_i2s = 1 (I2S mode) */
    d->ai_i2s = 1;

    /* AI_rj = 0 (not right justified) */
    d->ai_rj = 0;

    /* AI_m = 0 (not master mode) - default, not in registry */
    d->ai_m = 0;

    /*
     * ========================================
     * Video Output Configuration
     * ========================================
     */

    /* VOEnable = 0 (video output disabled on C985) */
    d->vo_enable = 0;

    /* VOUMode = 0 */
    d->vou_mode = 0;

    /* VOUStartPixel = 1 */
    d->vou_start_pixel = 1;

    /* VOUStartLine = 0 */
    d->vou_start_line = 0;

    /*
     * ========================================
     * Audio Output Configuration
     * ========================================
     */

    /* AOEnable = 1 (audio output enabled) */
    d->ao_enable = 1;

    /* AO_msb = 1 */
    d->ao_msb = 1;

    /* AO_lrclk = 1 */
    d->ao_lrclk = 1;

    /* AO_bclk = 0 */
    d->ao_bclk = 0;

    /* AO_i2s = 1 */
    d->ao_i2s = 1;

    /* AO_rj = 1 */
    d->ao_rj = 1;
    void c985_get_init_data(struct c985_poc *d);
    void c985_write_qpsos_config(struct c985_poc *d);
    /* AO_s = 0 */
    d->ao_s = 0;

    /*
     * ========================================
     * I2C Configuration
     * ========================================
     */

    /* I2CType = 2 (GPIO-based I2C) */
    d->i2c_type = 2;

    /* I2CGPIOClk = 0x0E (GPIO 14) */
    d->i2c_gpio_clk = 0x0E;

    /* I2CGPIOData = 0x0F (GPIO 15) */
    d->i2c_gpio_data = 0x0F;

    /* I2CExType = 5 */
    d->i2c_ex_type = 5;

    /*
     * ========================================
     * C985-Specific Hardware Configuration
     * ========================================
     */

    /* C985McuAddr = 0x2B (NUC100 I2C address, 7-bit) */
    d->mcu_addr = 0x2B;

    /* C985McuRst = 5 (GPIO 5 for MCU reset) */
    d->mcu_rst_gpio = 5;

    /* C985AlgAudAddr = 0x30 (TI3101 I2C address) */
    d->alg_aud_addr = 0x30;

    /* C985AlgAudRst = 6 (GPIO 6 for audio chip reset) */
    d->alg_aud_rst_gpio = 6;

    /* C985AudSwitch1 = 0x0D (GPIO 13) */
    d->aud_switch_gpio1 = 0x0D;

    /* C985AudSwitch2 = 7 (GPIO 7) */
    d->aud_switch_gpio2 = 7;

    /*
     * ========================================
     * PLL Overrides (0 = use chip defaults)
     * ========================================
     */

    d->pll4_override = 0;
    d->pll5_override = 0;

    /*
     * ========================================
     * Encoder Configuration (from [Encoder] registry key)
     * ========================================
     */

    /* EncFunction = 0x80000011 (SystemOpen function code) */
    d->enc_function = 0x80000011;

    /* EncSystemControl = 0x2101B219 */
    d->enc_system_control = 0x2101B219;

    /* EncRateControl = 0x005003E8 */
    d->enc_rate_control = 0x005003E8;

    /* EncRateControlEx = 0x00014050 */
    d->enc_rate_control_ex = 0x00014050;

    /* EncGopLoopFilter = 0xF199003C */
    d->enc_gop_loop_filter = 0xF199003C;

    /* EncPictureResolution = 0x02D00500 (1280x720) */
    d->enc_picture_resolution = 0x02D00500;

    /* EncOutPictureResolution = 0x02D00500 */
    d->enc_out_pic_resolution = 0x02D00500;

    /* EncInputControl = 0x0F7C0609 */
    d->enc_input_control = 0x0F7C0609;

    /* EncSyncMode = 0x00000011 */
    d->enc_sync_mode = 0x00000011;

    /* EncBitRate = 0x1F4007D0 */
    d->enc_bit_rate = 0x1F4007D0;

    /* EncFilterControl = 0x80002000 */
    d->enc_filter_control = 0x80002000;

    /* EncEtControl = 0 */
    d->enc_et_control = 0;

    /* EncBlockXferSize = 0x10 */
    d->enc_block_size = 0x10;

    /* EncStopMode = 0 */
    d->enc_stop_mode = 0;

    /* EncEnableVidPadding = 1 */
    d->enc_enable_vid_padding = 1;

    /* Encoder links */
    /* EncLinkVIn = 0x08 */
    d->enc_link_vin = 0x08;

    /* EncLinkVOut = 0x01 */
    d->enc_link_vout = 0x01;

    /* EncLinkAIn = 0x08 */
    d->enc_link_ain = 0x08;

    /* EncLinkAOut = 0x01 */
    d->enc_link_aout = 0x01;

    /* Audio encoding parameters */
    /* EncAudioControlParam = 0x21121080 */
    d->enc_audio_control_param = 0x21121080;

    /* EncAudioControlExAAC = 0x520800F2 */
    d->enc_audio_control_ex_aac = 0x520800F2;

    /* EncAudioControlExG711 = 0 */
    d->enc_audio_control_ex_g711 = 0;

    /* EncAudioControlExLPCM = 0x00000200 */
    d->enc_audio_control_ex_lpcm = 0x00000200;

    /* EncAudioControlExSILK = 0x02 */
    d->enc_audio_control_ex_silk = 0x02;

    /* Advanced encoder settings */
    /* EncLargeCompressBufferControl = 0x80004A38 */
    d->enc_large_compress_buf_ctrl = 0x80004A38;

    /* EncMjpegQuality = 0x0B */
    d->enc_mjpeg_quality = 0x0B;

    /* EncMjpegFrameBuffer = 0 */
    d->enc_mjpeg_frame_buffer = 0;

    /* EncIndexCapFreq = 0x20 */
    d->enc_index_cap_freq = 0x20;

    /* EncMP4VideoBlockNumber = 5 */
    d->enc_mp4_video_block_number = 5;

    /* Decimation/scaling */
    /* EncDecimationInputFormat = 0 */
    d->enc_decimation_input_format = 0;

    /* EncDecimationOutputFormat = 1 */
    d->enc_decimation_output_format = 1;

    /* EncDecimationScaleFactor = 0x01040104 */
    d->enc_decimation_scale_factor = 0x01040104;

    /* EncDeinterlaceMode = 1 */
    d->enc_deinterlace_mode = 1;

    /*
     * ========================================
     * Computed/Combined Values
     * ========================================
     */

    /* Build combined audio input control word */
    d->aud_controls = (d->ai_msb << 0) |
    (d->ai_lrclk << 1) |
    (d->ai_bclk << 2) |
    (d->ai_i2s << 3) |
    (d->ai_rj << 4) |
    (d->ai_m << 5);

    /* Build combined audio output control word */
    d->ao_controls = (d->ao_msb << 0) |
    (d->ao_lrclk << 1) |
    (d->ao_bclk << 2) |
    (d->ao_i2s << 3) |
    (d->ao_rj << 4) |
    (d->ao_s << 5);

    /*
     * ========================================
     * Video/Tuner Types (for completeness)
     * ========================================
     */

    /* VidOutputType = 0 */
    /* VidOutputStd = 1 */
    /* TunerType = 0 */
    /* TVAudioType = 0 */
    /* AudioCodecType = 0 */

    dev_info(&d->pdev->dev, "Configuration initialized:\n");
    dev_info(&d->pdev->dev, "  ChipType=%d AccessMode=%d BusType=%d\n",
             d->chip_type, d->access_mode, d->bus_type);
    dev_info(&d->pdev->dev, "  VerFwAPI=%d FwFixedMode=%d FwIntMode=%d\n",
             d->ver_fw_api, d->fw_fixed_mode, d->fw_int_mode);
    dev_info(&d->pdev->dev, "  VidInputType=0x%X VIUMode=%d\n",
             d->vid_input_type, d->viu_mode);
    dev_info(&d->pdev->dev, "  AOEnable=%d VOEnable=%d\n",
             d->ao_enable, d->vo_enable);
    dev_info(&d->pdev->dev, "  MCU=0x%02X@GPIO%d TI3101=0x%02X@GPIO%d\n",
             d->mcu_addr, d->mcu_rst_gpio,
             d->alg_aud_addr, d->alg_aud_rst_gpio);
    void c985_get_init_data(struct c985_poc *d);
    void c985_write_qpsos_config(struct c985_poc *d);
}
