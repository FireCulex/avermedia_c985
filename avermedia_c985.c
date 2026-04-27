// SPDX-License-Identifier: GPL-2.0
// avermedia_c985.c — PCI driver wrapper for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "avermedia_c985.h"
#include "structs.h"
#include "queue.h"
#include "cqlcodec.h"
#include "ti3101.h"
#include "nuc100.h"
#include "project.h"
#include "v4l2.h"
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "pciecntl.h"
#include "firmware.h"
#include "interrupts.h"
#include "qpfwencapi.h"
#include "cqlcodec_done.h"
#include "dma.h"
#include "include/abi/cqueue.h"

MODULE_DESCRIPTION(DRV_DESC);

static int c985_buffers_show(struct seq_file *m, void *v)
{
    struct c985_poc *d = m->private;
    struct c985_buffer *buf;
    unsigned long flags;
    int count = 0;

    seq_printf(m, "=== V4L2 Buffer Queue State ===\n");
    seq_printf(m, "Device: /dev/video%d\n", d->vdev.num);
    seq_printf(m, "Encoder Running: %s\n", d->encoder_running ? "YES" : "NO");
    seq_printf(m, "Sequence: %u\n\n", d->sequence);

    /* Show main buffer list */
    spin_lock_irqsave(&d->buf_lock, flags);
    seq_printf(m, "--- Main Buffer List (buf_list) ---\n");
    list_for_each_entry(buf, &d->buf_list, list) {
        seq_printf(m, "  Buffer %d: vbuf=%pK desc=%pK header=%pK entry=%pK\n",
                   count++,
                   &buf->vb.vb2_buf,
                   buf->buf_desc,
                   buf->header,
                   buf->queue_entry);

        if (buf->buf_desc) {
            seq_printf(m, "    Offset: %u / Size: %u / Flags: 0x%08x\n",
                       buf->buf_desc->ulBufferOffset,
                       buf->buf_desc->ulBufferSize,
                       buf->buf_desc->ulFlags);
        }
    }
    spin_unlock_irqrestore(&d->buf_lock, flags);

    /* Show channel queue states if task is active */
    if (d->codec.m_pTask) {
        int i;
        seq_printf(m, "\n=== Task Channel Queues ===\n");

        for (i = 0; i < 8; i++) {
            struct task_data *td = &d->codec.m_pTask->m_TaskData[i];

            if (td->valid && td->pChannel[4]) {  /* YUV channel */
                struct c_channel *ch = td->pChannel[4];
                seq_printf(m, "\nTask %d (YUV Input):\n", i);
                seq_printf(m, "  State: %d\n", td->m_State);
                seq_printf(m, "  Channel: %pK\n", ch);

                if (ch->m_pFreeQueue)
                    seq_printf(m, "  Free Queue Entries: %u\n", ch->m_pFreeQueue->m_dwNbInQueue);
                if (ch->m_pDataRequestQueue)
                    seq_printf(m, "  Request Queue Entries: %u\n", ch->m_pDataRequestQueue->m_dwNbInQueue);
                if (ch->m_pDataPendingQueue)
                    seq_printf(m, "  Pending Queue Entries: %u\n", ch->m_pDataPendingQueue->m_dwNbInQueue);

                seq_printf(m, "  UserBuffer pBufDesc: %pK\n", td->UserBuffer[4].pBufDesc);
                seq_printf(m, "  ArmRequest valid: %u\n", td->ArmRequest[4].ArmBuffer.BUFFER.ALL.valid);
                seq_printf(m, "  ArmRequest reserved4: %u (Y offset)\n",
                           td->ArmRequest[4].ArmBuffer.BUFFER.ALL.reserved4);
            }
        }
    }

    return 0;
}

static int c985_buffers_open(struct inode *inode, struct file *file)
{
    return single_open(file, c985_buffers_show, inode->i_private);
}

static const struct file_operations c985_buffers_fops = {
    .owner = THIS_MODULE,
    .open = c985_buffers_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};


static long PciStartDevice(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "PciStartDevice()\n");

    /* Check if we have an interrupt */
    if (d->pcie.InterruptLevel != 0) {
        /*
         * IoInitializeDpcRequest(FDO, PciDpcForIsr)
         * KeInitializeDpc(&Dpc, PciDpcForIsrArmMsg, ctx)
         *
         * Linux equivalent: INIT_WORK already done in CQLCodec_InitDevice
         */

        /* Register ISR */
        ret = CPCIeCntl_RegisterISR(d);
        if (ret < 0) {
            dev_err(&d->pdev->dev,
                    "PciStartDevice() IoConnectInterrupt Error=0x%X\n", ret);
            d->pcie.m_InterruptAvailable = 0;
        } else {
            d->pcie.m_InterruptAvailable = 1;
        }
    }

    /* PciValidateConfig - skip for now, not critical */

    /* PedDmaInit */
    ret = PedDmaInit(d);
    if (ret == -ENODEV) {
        /* STATUS_DEVICE_DOES_NOT_EXIST = -0x3fffff40 in Windows */
        /* Not fatal, continue */
        ret = 0;
    }
    if (ret < 0) {
        dev_err(&d->pdev->dev, "PciStartDevice() PedDmaInit failed: %d\n", ret);
        return ret;
    }

    /* PciStartWatchdogTimer - skip for now */

    dev_info(&d->pdev->dev, "PciStartDevice() complete\n");
    return 0;
}



static int CDEVICE__Init(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;
    int i;

    pdev->class = PCI_CLASS_MULTIMEDIA_VIDEO << 8;

    /* === Struct Size Debug === */
    dev_info(&pdev->dev, "=== Struct Size Debug ===\n");
    dev_info(&pdev->dev, "sizeof(struct CObject) = %zu (expected 56)\n", sizeof(struct CObject));
    dev_info(&pdev->dev, "sizeof(struct task_user_buffer) = %zu (expected 48)\n", sizeof(struct task_user_buffer));
    dev_info(&pdev->dev, "sizeof(struct task_arm_buffer) = %zu (expected 60)\n", sizeof(struct task_arm_buffer));
    dev_info(&pdev->dev, "sizeof(struct task_arm_request) = %zu (expected 72)\n", sizeof(struct task_arm_request));
    dev_info(&pdev->dev, "sizeof(struct task_data) = %zu (expected 0x64C = %d)\n", sizeof(struct task_data), 0x64C);  /* ← ADD THIS */
    dev_info(&pdev->dev, "sizeof(struct host_message) = %zu\n", sizeof(struct host_message));
    dev_info(&pdev->dev, "sizeof(struct host_message_status) = %zu\n", sizeof(struct host_message_status));
    dev_info(&pdev->dev, "sizeof(enum arm_buffer_type) = %zu\n", sizeof(enum arm_buffer_type));
    dev_info(&pdev->dev, "sizeof(spinlock_t) = %zu\n", sizeof(spinlock_t));
    dev_info(&pdev->dev, "sizeof(struct t_event_block) = %zu (expected 0x318 = 792)\n", sizeof(struct t_event_block));
    dev_info(&pdev->dev, "sizeof(struct completion) = %zu\n", sizeof(struct completion));
    dev_info(&pdev->dev, "offsetof(struct task_data, pBufDescToCancel) = %zu (expected 0x3C8)\n", offsetof(struct task_data, pBufDescToCancel));
    dev_info(&pdev->dev, "offsetof(struct task_data, pChannel) = %zu (expected 0x41C)\n", offsetof(struct task_data, pChannel));
    dev_info(&pdev->dev, "offsetof(struct task_data, pArmMsgFifo) = %zu (expected 0x48C)\n", offsetof(struct task_data, pArmMsgFifo));
    dev_info(&pdev->dev, "offsetof(struct task_data, m_EvtReply) = %zu (expected 0x644)\n", offsetof(struct task_data, m_EvtReply));
    dev_info(&pdev->dev, "sizeof(struct arm_buffer_all) = %zu (expected 56)\n", sizeof(struct arm_buffer_all));
    dev_info(&pdev->dev, "sizeof(struct c_channel) = %zu (expected 0x1190 = %zu)\n", sizeof(struct c_channel), (size_t)0x1190);
    dev_info(&pdev->dev, "sizeof(struct c_queue) = %zu (expected 0x50 = %zu)\n", sizeof(struct c_queue), (size_t)0x50);
    dev_info(&pdev->dev, "sizeof(struct IMpegCodec) = %zu (expected 0xC8 = %zu)\n", sizeof(struct IMpegCodec), (size_t)0xC8);
    dev_info(&pdev->dev, "sizeof(struct c_channel) = %zu (expected 0x1190 = %zu)\n", sizeof(struct c_channel), (size_t)0x1190);
    pr_info("C985: sizeof(struct c985_poc) = %zu\n", sizeof(struct c985_poc));




    /* ========================= */


    ret = CQLCodec_InitDevice(pdev, id);
    if (ret)
        return ret;

    d = pci_get_drvdata(pdev);

    d->codec.m_pDeviceCallback = (void *)CQLCodecLibBusCallback;
    d->codec.m_callbackContext = d;
    d->pcie.pBusCallbackFunc = (void *)CQLCodecLibBusCallback;
    d->pcie.pBusCallbackContext = d;

    /* Scan for available DMA channels */
    d->pcie.m_NumDmaAvailable = 0;
    for (i = 0; i < 64; i++) {
        u32 caps = readl(c985_bar0(d) + (i * 0x100));
        if (caps & 0x01) {
            d->pcie.m_NumDmaAvailable++;
        }
    }
    dev_info(&pdev->dev, "Found %d DMA channels\n", d->pcie.m_NumDmaAvailable);

    /* Set InterruptLevel from PCI device */
    d->pcie.InterruptLevel = d->pdev->irq ? 1 : 0;

    /* PciStartDevice: Register ISR + DMA init */
    ret = PciStartDevice(d);
    if (ret)
        goto err_remove;

    if (CQLCodec_Constructor(d, NULL, 0, NULL, &d->pcie) == NULL) {
        dev_err(&pdev->dev, "CQLCodec_Constructor failed\n");
        ret = -ENOMEM;
        goto err_remove;
    }

    ret = CQLCodec_FWDownloadAll(d, 0, 1);
    if (ret)
        goto err_remove;

    ret = project_c985_init(d);
    if (ret)
        goto err_remove;

    return 0;

    err_remove:
    c985_pci_remove(pdev);
    return ret;
}

static const struct pci_device_id c985_pci_ids[] = {
    { PCI_DEVICE(0x1af2, 0xa001) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, c985_pci_ids);

static struct pci_driver c985_pci_driver = {
    .name     = DRV_NAME,
    .id_table = c985_pci_ids,
    .probe    = CDEVICE__Init,
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
    d->codec.m_VOEnable = 1;              /* disabled on C985 */
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

static int c985_regs_show(struct seq_file *m, void *v)
{
    struct c985_poc *d = m->private;

    seq_printf(m, "=== Core Registers ===\n");
    seq_printf(m, "0x00  Chip control:      0x%08x\n", readl(c985_bar1(d) + 0x00));
    seq_printf(m, "0x04  IRQ status/enable: 0x%08x\n", readl(c985_bar1(d) + 0x04));
    seq_printf(m, "0x08  Unknown:           0x%08x\n", readl(c985_bar1(d) + 0x08));
    seq_printf(m, "0x0C  IRQ mask:          0x%08x\n", readl(c985_bar1(d) + 0x0C));
    seq_printf(m, "0x10  Timer control:     0x%08x\n", readl(c985_bar1(d) + 0x10));
    seq_printf(m, "0x14  Timer counter:     0x%08x\n", readl(c985_bar1(d) + 0x14));
    seq_printf(m, "0x18  Timer compare:     0x%08x\n", readl(c985_bar1(d) + 0x18));
    seq_printf(m, "0x24  Doorbell:          0x%08x\n", readl(c985_bar1(d) + 0x24));
    seq_printf(m, "0x38  Chip version:      0x%08x\n", readl(c985_bar1(d) + 0x38));

    seq_printf(m, "\n=== MCU Interface ===\n");
    seq_printf(m, "0x100 NUC100 ctrl:       0x%08x\n", readl(c985_bar1(d) + 0x100));
    seq_printf(m, "0x104 NUC100 data:       0x%08x\n", readl(c985_bar1(d) + 0x104));

    seq_printf(m, "\n=== Encoder Status (0x300) ===\n");
    seq_printf(m, "0x300 Enable?:           0x%08x\n", readl(c985_bar1(d) + 0x300));
    seq_printf(m, "0x304:                   0x%08x\n", readl(c985_bar1(d) + 0x304));
    seq_printf(m, "0x308:                   0x%08x\n", readl(c985_bar1(d) + 0x308));
    seq_printf(m, "0x310:                   0x%08x\n", readl(c985_bar1(d) + 0x310));
    seq_printf(m, "0x314:                   0x%08x\n", readl(c985_bar1(d) + 0x314));
    seq_printf(m, "0x320:                   0x%08x\n", readl(c985_bar1(d) + 0x320));
    seq_printf(m, "0x340:                   0x%08x\n", readl(c985_bar1(d) + 0x340));
    seq_printf(m, "0x380:                   0x%08x\n", readl(c985_bar1(d) + 0x380));
    seq_printf(m, "0x390:                   0x%08x\n", readl(c985_bar1(d) + 0x390));
    seq_printf(m, "0x3A0:                   0x%08x\n", readl(c985_bar1(d) + 0x3A0));
    seq_printf(m, "0x3A4:                   0x%08x\n", readl(c985_bar1(d) + 0x3A4));
    seq_printf(m, "0x3A8:                   0x%08x\n", readl(c985_bar1(d) + 0x3A8));

    seq_printf(m, "\n=== Interrupt ===\n");
    seq_printf(m, "0x600 IRQ mode:          0x%08x\n", readl(c985_bar1(d) + 0x600));
    seq_printf(m, "0x61C Secondary IRQ:     0x%08x\n", readl(c985_bar1(d) + 0x61C));

    seq_printf(m, "\n=== Mailbox ===\n");
    seq_printf(m, "0x6B0 Response[0]:       0x%08x\n", readl(c985_bar1(d) + 0x6B0));
    seq_printf(m, "0x6B4 Response[1]:       0x%08x\n", readl(c985_bar1(d) + 0x6B4));
    seq_printf(m, "0x6B8 Response[2]:       0x%08x\n", readl(c985_bar1(d) + 0x6B8));
    seq_printf(m, "0x6BC Response[3]:       0x%08x\n", readl(c985_bar1(d) + 0x6BC));
    seq_printf(m, "0x6C0 Response[4]:       0x%08x\n", readl(c985_bar1(d) + 0x6C0));
    seq_printf(m, "0x6C4 Response[5]:       0x%08x\n", readl(c985_bar1(d) + 0x6C4));
    seq_printf(m, "0x6C8 ARM response:      0x%08x\n", readl(c985_bar1(d) + 0x6C8));
    seq_printf(m, "0x6CC Mailbox cmd:       0x%08x\n", readl(c985_bar1(d) + 0x6CC));
    seq_printf(m, "0x6E4 Mailbox reg[0]:    0x%08x\n", readl(c985_bar1(d) + 0x6E4));
    seq_printf(m, "0x6E8 Mailbox reg[1]:    0x%08x\n", readl(c985_bar1(d) + 0x6E8));
    seq_printf(m, "0x6EC Mailbox reg[2]:    0x%08x\n", readl(c985_bar1(d) + 0x6EC));
    seq_printf(m, "0x6F0 Mailbox reg[3]:    0x%08x\n", readl(c985_bar1(d) + 0x6F0));
    seq_printf(m, "0x6F4 Encoder/MB reg[4]: 0x%08x\n", readl(c985_bar1(d) + 0x6F4));
    seq_printf(m, "0x6F8 Function code:     0x%08x\n", readl(c985_bar1(d) + 0x6F8));
    seq_printf(m, "0x6FC Mailbox data:      0x%08x\n", readl(c985_bar1(d) + 0x6FC));

    seq_printf(m, "\n=== HCI/ARM ===\n");
    seq_printf(m, "0x800 HCI IRQ enable:    0x%08x\n", readl(c985_bar1(d) + 0x800));
    seq_printf(m, "0x80C ARM boot flag:     0x%08x\n", readl(c985_bar1(d) + 0x80C));
    seq_printf(m, "0x840 HCI control:       0x%08x\n", readl(c985_bar1(d) + 0x840));

    seq_printf(m, "\n=== DMA (BAR0) ===\n");
    seq_printf(m, "0x00  DMA caps:          0x%08x\n", readl(c985_bar0(d) + 0x00));
    seq_printf(m, "0x04  DMA ctrl:          0x%08x\n", readl(c985_bar0(d) + 0x04));
    seq_printf(m, "0x30  Global IRQ:        0x%08x\n", readl(c985_bar0(d) + 0x30));
    seq_printf(m, "0x4000 PCI IRQ status:   0x%08x\n", readl(c985_bar1(d) + 0x4000));

    return 0;
}


static int c985_regs_open(struct inode *inode, struct file *file)
{
    return single_open(file, c985_regs_show, inode->i_private);
}

static const struct file_operations c985_regs_fops = {
    .owner = THIS_MODULE,
    .open = c985_regs_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};


void c985_debugfs_init(struct c985_poc *d)
{
    d->debug_dir = debugfs_create_dir("c985", NULL);
    if (IS_ERR_OR_NULL(d->debug_dir)) {
        dev_warn(&d->pdev->dev, "Failed to create debugfs dir\n");
        return;
    }

    debugfs_create_file("regs", 0444, d->debug_dir, d, &c985_regs_fops);

    /* Buffer queue state entry */
    debugfs_create_file("buffers", 0444, d->debug_dir, d, &c985_buffers_fops);

    dev_info(&d->pdev->dev, "debugfs created at /sys/kernel/debug/c985/\n");
}

void c985_debugfs_cleanup(struct c985_poc *d)
{
    debugfs_remove_recursive(d->debug_dir);
}
/**
 * PciStartDevice - Initialize PCIe device resources
 *
 * Windows equivalent sets up:
 * 1. DPC handlers for interrupts
 * 2. Register ISR
 * 3. Set m_InterruptAvailable
 * 4. Validate PCI config
 * 5. Initialize DMA
 * 6. Start watchdog timer
 */

