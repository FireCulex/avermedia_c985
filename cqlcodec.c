// SPDX-License-Identifier: GPL-2.0
// cqlcodec.c — CQLCodec device initialization for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/delay.h>

#include "avermedia_c985.h"
#include "cqlcodec.h"
#include "cpr.h"
#include "qphci.h"
#include "qpfwapi.h"
#include "firmware.h"
#include "structs.h"

#include "interrupts.h"

/* Forward declaration */
static void cqlcodec_interrupt_handler(struct work_struct *work);

/* -----------------------------------------------------------------------
 * Register helpers
 * --------------------------------------------------------------------- */
static inline void c985_wr(struct c985_poc *d, u32 off, u32 val)
{
    dev_dbg(&d->pdev->dev, "WR [0x%04x] = 0x%08x\n", off, val);
    writel(val, c985_bar1(d) + off);
}

static inline u32 c985_rd(struct c985_poc *d, u32 off)
{
    u32 val = readl(c985_bar1(d) + off);
    dev_dbg(&d->pdev->dev, "RD [0x%04x] = 0x%08x\n", off, val);
    return val;
}

/* -----------------------------------------------------------------------
 * Debug helpers
 * --------------------------------------------------------------------- */
static void dump_full_state(struct c985_poc *d, const char *tag)
{
    dev_info(&d->pdev->dev, "========== %s ==========\n", tag);
    dev_info(&d->pdev->dev, "[%s] IRQ: 0x04=0x%08x 0x24=0x%08x 0x4000=0x%08x 0x4030=0x%08x\n",
             tag,
             readl(c985_bar1(d) + 0x04),
             readl(c985_bar1(d) + 0x24),
             readl(c985_bar1(d) + 0x4000),
             readl(c985_bar1(d) + 0x4030));
    dev_info(&d->pdev->dev, "[%s] HCI: 0x800=0x%08x 0x804=0x%08x 0x80C=0x%08x\n",
             tag,
             readl(c985_bar1(d) + 0x800),
             readl(c985_bar1(d) + 0x804),
             readl(c985_bar1(d) + 0x80C));
    dev_info(&d->pdev->dev, "[%s] MAILBOX: 0x6C8=0x%08x 0x6CC=0x%08x 0x6FC=0x%08x\n",
             tag,
             readl(c985_bar1(d) + 0x6C8),
             readl(c985_bar1(d) + 0x6CC),
             readl(c985_bar1(d) + 0x6FC));
    dev_info(&d->pdev->dev, "[%s] ARM: RESET=0x%08x BOOT=0x%08x CTRL=0x%08x\n",
             tag,
             readl(c985_bar1(d) + 0x800),
             readl(c985_bar1(d) + 0x80C),
             readl(c985_bar1(d) + 0x00));
}


/* -----------------------------------------------------------------------
 * Firmware upload via CPR
 * --------------------------------------------------------------------- */
static int upload_firmware_cpr(struct c985_poc *d, const char *path,
                               const char *label, u32 card_base)
{
    const struct firmware *fw;
    u32 sz4, i, word;
    int ret;

    ret = request_firmware(&fw, path, &d->pdev->dev);
    if (ret) {
        dev_err(&d->pdev->dev, "Cannot load %s: %d\n", path, ret);
        return ret;
    }

    dev_info(&d->pdev->dev, "FW %s: %zu bytes\n", label, fw->size);

    sz4 = ALIGN(fw->size, 4);
    for (i = 0; i < sz4; i += 4) {
        word = 0;
        if (i < fw->size)
            memcpy(&word, fw->data + i, min_t(u32, 4, fw->size - i));
        word = le32_to_cpu(word);

        ret = cpr_write(d, card_base + i, word);
        if (ret) {
            dev_err(&d->pdev->dev, "CPR write failed at 0x%x\n", i);
            break;
        }
    }

    release_firmware(fw);
    return ret;
}

/* -----------------------------------------------------------------------
 * Memory init
 * --------------------------------------------------------------------- */
// CQLCodec_InitializeMemory
static int codec_initialize_memory(struct c985_poc *d)
{
    u32 local_1c, local_18, local_20, local_24, local_28;
    int ret;

    local_1c = 7;
    local_18 = 2;
    local_20 = 0x20007;

    dev_dbg(&d->pdev->dev, "Initial: local_1c=%u local_18=%u local_20=0x%08x\n",
            local_1c, local_18, local_20);

    /* Write initial value to 0xf14 */
    dev_dbg(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    writel(local_20, c985_bar1(d) + 0x0f14);

    dev_vdbg(&d->pdev->dev, "CPR write addr=0x0 val=0x%08x\n", local_20);
    ret = cpr_write(d, 0, local_20);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR write #1 failed\n");
        return ret;
    }

    /* Row address detection loop */

    for (; local_1c > 3; local_1c--) {
        u32 addr = 1 << ((local_1c + 6) & 0x1f);
        u32 val = local_1c - 1;

        dev_vdbg(&d->pdev->dev, "Row loop: local_1c=%u addr=0x%08x val=%u\n",
                 local_1c, addr, val);

        ret = cpr_write(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write row failed at local_1c=%u\n", local_1c);
            return ret;
        }
    }

    dev_vdbg(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = cpr_read(d, 0, &local_24);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR read #1 failed\n");
        return ret;
    }

    local_1c = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;

    dev_vdbg(&d->pdev->dev, "Row detect result: read=0x%08x local_1c=%u local_20=0x%08x\n",
             local_24, local_1c, local_20);
    dev_vdbg(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    writel(local_20, c985_bar1(d) + 0x0f14);

    /* Column address detection */

    dev_vdbg(&d->pdev->dev, "CPR write addr=0x0 val=0x%08x\n", local_18);
    ret = cpr_write(d, 0, local_18);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR write #2 failed\n");
        return ret;
    }

    for (; local_18 > 1; local_18--) {
        u32 addr = 1 << ((local_1c + 0x15) & 0x1f);
        u32 val = local_18 - 1;

        dev_dbg(&d->pdev->dev, "Col loop: local_18=%u addr=0x%08x val=%u\n",
                local_18, addr, val);

        ret = cpr_write(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write col failed at local_18=%u\n", local_18);
            return ret;
        }
    }

    dev_dbg(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = cpr_read(d, 0, &local_24);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR read #2 failed\n");
        return ret;
    }

    local_18 = local_24 & 0xf;
    local_20 = (local_18 << 16) | local_1c;

    dev_vdbg(&d->pdev->dev, "Col detect result: read=0x%08x local_18=%u local_20=0x%08x\n",
             local_24, local_18, local_20);
    dev_vdbg(&d->pdev->dev, "Writing 0xf14 = 0x%08x\n", local_20);
    writel(local_20, c985_bar1(d) + 0x0f14);

    /* Final register configuration */

    local_28 = readl(c985_bar1(d) + 0x0f1c);
    dev_dbg(&d->pdev->dev, "Read 0xf1c = 0x%08x\n", local_28);
    dev_dbg(&d->pdev->dev, "Writing 0xf1c = 0x%08x\n", local_28 & 0xfffffcff);
    writel(local_28 & 0xfffffcff, c985_bar1(d) + 0x0f1c);

    dev_dbg(&d->pdev->dev, "Writing memory controller registers:\n");
    dev_dbg(&d->pdev->dev, "  0xf04 = 0x0d03110b\n");
    writel(0x0d03110b, c985_bar1(d) + 0x0f04);

    dev_dbg(&d->pdev->dev, "  0xf08 = 0x00000003\n");
    writel(0x00000003, c985_bar1(d) + 0x0f08);

    dev_dbg(&d->pdev->dev, "  0xf40 = 0x00000002\n");
    writel(0x00000002, c985_bar1(d) + 0x0f40);

    dev_dbg(&d->pdev->dev, "  0xf10 = 0x05140080\n");
    writel(0x05140080, c985_bar1(d) + 0x0f10);

    dev_dbg(&d->pdev->dev, "  0xf18 = 0x00000001\n");
    writel(0x00000001, c985_bar1(d) + 0x0f18);

    dev_dbg(&d->pdev->dev, "Waiting 100ms for memory stabilization...\n");
    msleep(100);

    {
        u32 test_val;
        cpr_write(d, 0, 0xAAAAAAAA);
        cpr_read(d, 0, &test_val);
        dev_dbg(&d->pdev->dev,
                "CPR[0x0] = 0x%08x (expect 0xAAAAAAAA) %s\n",
                test_val, test_val == 0xAAAAAAAA ? "OK" : "FAIL");

        cpr_write(d, 0x1000, 0x55555555);
        cpr_read(d, 0x1000, &test_val);
        dev_dbg(&d->pdev->dev,
                "CPR[0x1000] = 0x%08x (expect 0x55555555) %s\n",
                test_val, test_val == 0x55555555 ? "OK" : "FAIL");

        dev_dbg(&d->pdev->dev,
                "MEMCTL: 0xf04=0x%08x 0xf08=0x%08x 0xf10=0x%08x\n",
                readl(c985_bar1(d) + 0xf04),
                readl(c985_bar1(d) + 0xf08),
                readl(c985_bar1(d) + 0xf10));
        dev_dbg(&d->pdev->dev,
                "MEMCTL: 0xf14=0x%08x 0xf18=0x%08x 0xf1c=0x%08x 0xf40=0x%08x\n",
                readl(c985_bar1(d) + 0xf14),
                readl(c985_bar1(d) + 0xf18),
                readl(c985_bar1(d) + 0xf1c),
                readl(c985_bar1(d) + 0xf40));
    }

    return 0;
}

/* -----------------------------------------------------------------------
 * AO/VO switches
 * --------------------------------------------------------------------- */
void cqlcodec_ao_switch(struct c985_poc *d, int disable)
{
    u32 val = c985_rd(d, REG_AO_VO_CTL);
    if (disable)
        val &= ~AO_ENABLE_BIT;
    else
        val |= AO_ENABLE_BIT;
    c985_wr(d, REG_AO_VO_CTL, val);
}

void cqlcodec_vo_switch(struct c985_poc *d, int disable)
{
    u32 val = c985_rd(d, REG_AO_VO_CTL);
    if (disable)
        val &= ~VO_ENABLE_BIT;
    else
        val |= VO_ENABLE_BIT;
    c985_wr(d, REG_AO_VO_CTL, val);
}

/* -----------------------------------------------------------------------
 * Load default settings
 * --------------------------------------------------------------------- */
// CQLCodec_LoadDefaultSettings
void cqlcodec_load_default_settings(struct c985_poc *d)
{
    dev_vdbg(&d->pdev->dev, "CQLCodec: load default settings\n");

    d->codec.m_ENC_REG_MESSAGE             = ENC_REG_MESSAGE;
    d->codec.m_ENC_REG_SYSTEM_CONTROL      = ENC_REG_SYSTEM_CONTROL;
    d->codec.m_ENC_REG_PICTURE_RESOLUTION  = ENC_REG_PICTURE_RESOLUTION;
    d->codec.m_ENC_REG_INPUT_CONTROL       = ENC_REG_INPUT_CONTROL;
    d->codec.m_ENC_REG_RATE_CONTROL        = ENC_REG_RATE_CONTROL;
    d->codec.m_ENC_REG_BIT_RATE            = ENC_REG_BIT_RATE;
    d->codec.m_ENC_REG_FILTER_CONTROL      = ENC_REG_FILTER_CONTROL;
    d->codec.m_ENC_REG_GOP_LOOP_FILTER     = ENC_REG_GOP_LOOP_FILTER;
    d->codec.m_ENC_REG_OUT_PIC_RESOLUTION  = ENC_REG_OUT_PIC_RESOLUTION;
    d->codec.m_ENC_REG_ET_CONTROL          = ENC_REG_ET_CONTROL;
    d->codec.m_ENC_REG_BLOCK_SIZE          = ENC_REG_BLOCK_SIZE;
    d->codec.m_ENC_REG_AUDIO_CONTROL_PARAM = ENC_REG_AUDIO_CONTROL_PARAM;
    d->codec.m_ENC_REG_AUDIO_CONTROL_EX    = ENC_REG_AUDIO_CONTROL_EX;

    d->codec.m_VOEnable       = 1;
    d->codec.m_VOUMode        = 0;
    d->codec.m_VOUStartPixel  = 1;
    d->codec.m_VOUStartLine   = 0;

    d->codec.m_VerFwAPI = 1;

    d->codec.m_AOEnable    = 1;
    d->codec.m_AOControls  = DEFAULT_AO_CONTROLS;
    d->codec.m_AudControls = DEFAULT_AUD_CONTROLS;

    d->codec.m_AIVolume = 8;
    d->codec.m_AOVolume = 8;

    dev_dbg(&d->pdev->dev, "CQLCodec: defaults loaded\n");
}

/* -----------------------------------------------------------------------
 * GPIO defaults
 * --------------------------------------------------------------------- */
static void gpio_set_defaults(struct c985_poc *d)
{
    c985_wr(d, REG_GPIO_DIR, 0x00000000);
    c985_wr(d, REG_GPIO_VAL, 0x00000000);
}

/* -----------------------------------------------------------------------
 * DMA completion work handler
 * --------------------------------------------------------------------- */
static void dma_completion_handler(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, dma_work);
    int i;

    dev_dbg(&d->pdev->dev, "DMA work: status=0x%08x\n", d->dma_interrupt_status);

    /* Process each completed DMA channel */
    for (i = 0; i < d->pcie.m_NumDmaAvailable; i++) {
        if (d->dma_interrupt_status & (1 << i)) {
            dev_dbg(&d->pdev->dev, "DMA channel %d (engine %d) completed\n",
                    i, d->dma_engine_idx[i]);

            /* Signal completion to any waiters */
            complete(&d->dma_done);

            /* Clear the bit */
            d->dma_interrupt_status &= ~(1 << i);
        }
    }
}

/* -----------------------------------------------------------------------
 * Device init
 * --------------------------------------------------------------------- */
// CQLCodec_InitDevice
int cqlcodec_init_device(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;

    dev_dbg(&pdev->dev, "cqlcodec_init_device()\n");

    ret = pcim_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pci_request_region(pdev, 0, DRV_NAME);
    if (ret)
        return ret;

    ret = pci_request_region(pdev, C985_BAR_MMIO, DRV_NAME);
    if (ret)
        goto err_bar0;

    d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
    if (!d) {
        ret = -ENOMEM;
        goto err_region;
    }

    d->pdev = pdev;

    d->pcie.pRegisters = pci_ioremap_bar(pdev, 0);
    if (!d->pcie.pRegisters) {
        ret = -ENOMEM;
        goto err_region;
    }

    d->pcie.pRegistersEx = pci_ioremap_bar(pdev, C985_BAR_MMIO);
    if (!d->pcie.pRegistersEx) {
        ret = -ENOMEM;
        goto err_bar1;
    }

    pci_set_drvdata(pdev, d);

    /* Load Windows-compatible configuration */
    CDEVICE__getInitData(d);

    /* INIT_WORK(&d->irq_work, cqlcodec_interrupt_handler); */
    INIT_WORK(&d->dma_work, dma_completion_handler);
    /* INIT_WORK(&d->frame_work, encoder_frame_work_handler); */
    init_completion(&d->mailbox_complete);
    init_completion(&d->dma_done);
    spin_lock_init(&d->irq_lock);


    dev_dbg(&pdev->dev, "reg 0x00 = 0x%08x\n", readl(c985_bar1(d) + 0x00));
    // ...

    /* Load default settings */
    cqlcodec_load_default_settings(d);

    /* Step 1: QPHCI_Init */
    ret = qphci_init(d);
    if (ret)
        goto err_out;

    /* Rest of initialization... */
    ret = codec_initialize_memory(d);
    if (ret)
        goto err_out;

    ret = qphci_init_arm_loop(d);
    if (ret)
        goto err_out;

    gpio_set_defaults(d);

    ret = pci_interrupt_service_register(d);
    if (ret) {
        dev_warn(&pdev->dev, "ISR registration failed\n");
        goto err_out;
    }

    cpciectl_enable_interrupts(d);

    /* Use the configured values */
    cqlcodec_ao_switch(d, d->codec.m_AOEnable);
    cqlcodec_vo_switch(d, d->codec.m_VOEnable);

    dev_info(&pdev->dev, "CQLCodec init complete\n");
    return 0;

    err_out:
    /* cancel_work_sync(&d->irq_work); */
    cancel_work_sync(&d->dma_work);
    /* cancel_work_sync(&d->frame_work); */
    if (d->irq_registered)
        free_irq(d->pdev->irq, d);
    iounmap(d->pcie.pRegistersEx);
    err_bar1:
    iounmap(d->pcie.pRegisters);
    err_region:
    pci_release_region(pdev, C985_BAR_MMIO);
    err_bar0:
    pci_release_region(pdev, 0);
    return ret;
}

/* -----------------------------------------------------------------------
 * Device remove
 * --------------------------------------------------------------------- */
void cqlcodec_remove_device(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "cqlcodec_remove_device()\n");

    if (!d)
        return;

    /* cancel_work_sync(&d->irq_work); */
    /* cancel_work_sync(&d->frame_work); */

    if (d->pcie.pRegisters) {
        /* Disable interrupt generation */
        u32 val = readl(c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
        writel(val & ~PED_GLOBAL_INT_ENABLE, c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
    }

    if (d->pcie.pRegistersEx) {
        /* Clear pending status */
        writel(readl(c985_bar1(d) + REG_PCI_INT_STATUS), c985_bar1(d) + REG_PCI_INT_STATUS);
        writel(0x00070000, c985_bar1(d) + 0x804);
        writel(0, c985_bar1(d) + 0x6C8);
        writel(0, c985_bar1(d) + 0x6CC);
    }

    if (d->irq_registered) {
        free_irq(d->pdev->irq, d);
        d->irq_registered = 0;
    }

    if (d->pcie.pRegistersEx) {
        iounmap(d->pcie.pRegistersEx);
        d->pcie.pRegistersEx = NULL;
    }

    if (d->pcie.pRegisters) {
        iounmap(d->pcie.pRegisters);
        d->pcie.pRegisters = NULL;
    }

    pci_release_region(pdev, C985_BAR_MMIO);
    pci_release_region(pdev, 0);

    dev_info(&pdev->dev, "hardware shutdown complete\n");
}

/* -----------------------------------------------------------------------
 * Firmware download
 * --------------------------------------------------------------------- */
// CQLCodec_FWDownloadAll
int cqlcodec_fw_download(struct c985_poc *d, int do_reset)
{
    if (!do_reset) {
        return 0;  /* Nothing to do */
    }

    return firmware_download_all(d);
}

static int cqlcodec_reset(struct c985_poc *d)
{
    int ret;

    dev_info(&d->pdev->dev, "CQLCodec_Reset()\n");

    /* 1-2. Clear mailboxes */
    writel(0, c985_bar1(d) + 0x6CC);
    writel(0, c985_bar1(d) + 0x6C8);

    /* 3. Skip AllocEncodeTask(0) for now */

    /* 4. QPHCI_ReInit */
    ret = qphci_reinit(d);
    if (ret)
        return ret;

    /* 5. InitializeMemory - but DDR should already be up! */
    /* Skip on reload, only needed on fresh boot */

    /* 6-8. AO/VO/GPIO */
    cqlcodec_ao_switch(d, !d->codec.m_AOEnable);
    cqlcodec_vo_switch(d, !d->codec.m_VOEnable);
    writel(0, c985_bar1(d) + REG_GPIO_DIR);
    writel(0, c985_bar1(d) + REG_GPIO_VAL);

    /* 9. Delay */
    msleep(5);

    /* 10. Skip AllocEncodeTask(1) for now */

    return 0;
}
