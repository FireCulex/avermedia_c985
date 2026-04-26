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
#include "qpfwencapi.h"
#include "ctask/ctask_private.h"
#include "cobject.h"
#include "interrupts.h"
#include "structs.h"
#include "pciecntl.h"
#include "channel.h"
#include "qpmm.h"

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

int CQLCodecLib_Init(struct cql_codec_lib *param_1)
{
    enum qpi2c_type i2cType;
    struct cql_codec *pCodec;
    int ret;

    pr_debug("CQLCodecLib_Init()\n");

    /* Acquire lock based on object attributes */
    /*
    if ((param_1->m_Object.m_dwObjectAttributes & 1) == 0) {
        if ((param_1->m_Object.m_dwObjectAttributes & 2) && param_1->m_Object.m_semCriticalSection)
            QPOSMWaitSem(param_1->m_Object.m_semCriticalSection, -1);
    } else {
        param_1->m_Object.m_irql = KeAcquireSpinLockRaiseToDpc(&param_1->m_Object.m_spinlock);
    } */

    /* Handle BusType */
    if (param_1->m_InitData.mpgCodecInitData.BusType == QPHCI_BUS_USB) {
        u32 *pBusData = param_1->m_InitData.mpgCodecInitData.BusData;

        if (!pBusData || param_1->m_InitData.mpgCodecInitData.BusDataSize != 0x10) {
             pr_debug("CQLCodecLib_Init() invalide USB bus data(0x%x) size(%d)\n");
            goto FAIL;
        }

        if (!CQLCodecLib_CreateInitUSB(param_1, pBusData[0], pBusData[1], pBusData[2], pBusData[3],
            param_1->m_InitData.mpgCodecInitData.AccessMode))
            goto FAIL;

        i2cType = QPI2C_TYPE_USE_USB;
    }
    else if (param_1->m_InitData.mpgCodecInitData.BusType == QPHCI_BUS_201_EMULATION) {
        i2cType = QPI2C_TYPE_USE_201_EMULATION;
    }
    else if (param_1->m_InitData.mpgCodecInitData.BusType == QPHCI_BUS_PCI) {
        if (!CQLCodecLib_CreatePCIe(param_1))
            goto FAIL;

        if (CQLCodecLib_InitPCIe(param_1) < 0) {
            pr_debug("CQLCodecLib_InitDevice() CQLCodecLib_InitPCIe() Failed status(%d)!!!\n");
            goto FAIL;
        }
        i2cType = QPI2C_TYPE_USE_QLCODEC;
    }
    else if (param_1->m_InitData.mpgCodecInitData.BusType == QPHCI_BUS_INTERNAL) {
        i2cType = QPI2C_TYPE_SOC_INTERNAL_HW;
    }
    else {
        i2cType = QPI2C_TYPE_USE_QLCODEC;
    }

    /* Set I2C types if out of valid range [1,6] */
    if (param_1->m_InitData.i2cInitData.type < 1 || param_1->m_InitData.i2cInitData.type > 6)
        param_1->m_InitData.i2cInitData.type = i2cType;

    if (param_1->m_InitData.i2cInitDataEx.type < 1 || param_1->m_InitData.i2cInitDataEx.type > 6)
        param_1->m_InitData.i2cInitDataEx.type = QPI2C_TYPE_DUMMY;

    /* Allocate and construct CQLCodec */
    pCodec = QPMMMalloc2Ex(0x430);
    if (!pCodec) {
        pr_debug("CQLCodecLib_Init() pQLCodec == NULL!!!\n");
        goto FAIL;
    }

    memset(pCodec, 0, 0x430);

    pCodec = (struct cql_codec *)CQLCodec_Constructor((struct c985_poc *)pCodec, &param_1->m_Object, 2, param_1->m_pUsbCntl, param_1->m_pPCIeCntl);

    /* pCodec = CQLCodec_Constructor(pCodec, &param_1->m_Object, 2, param_1->m_pUsbCntl, param_1->m_pPCIeCntl); */
    param_1->m_pMpgCodec = pCodec;

    if (!param_1->m_pMpgCodec) {
        pr_debug("CQLCodecLib_Init() CQLCodec_Constructor() failed!!!\n");
        /* QPMMFree2Ex(pCodec); */
        goto FAIL;
    }

    if (!CQLCodecLib_CreatePeripherals(param_1))
        goto FAIL;

    /* Success: Release lock and call base Init */
    /*
    if ((param_1->m_Object.m_dwObjectAttributes & 1) == 0) {
        if ((param_1->m_Object.m_dwObjectAttributes & 2) && param_1->m_Object.m_semCriticalSection)
             QPOSMSignalSem(param_1->m_Object.m_semCriticalSection);
    } else {
         KeReleaseSpinLock(&param_1->m_Object.m_spinlock, param_1->m_Object.m_irql);
    }
*/
    return param_1->m_Object.Init(&param_1->m_Object);

    FAIL:
    /* Failure: Release lock and return 0 */
    /*
    if ((param_1->m_Object.m_dwObjectAttributes & 1) == 0) {
        if ((param_1->m_Object.m_dwObjectAttributes & 2) && param_1->m_Object.m_semCriticalSection)
            QPOSMSignalSem(param_1->m_Object.m_semCriticalSection);
    } else {
        KeReleaseSpinLock(&param_1->m_Object.m_spinlock, param_1->m_Object.m_irql);
    }
*/
    return 0;
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

        ret = CPR_MemoryWrite(d, card_base + i, word);
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
static int CQLCodec_InitializeMemory(struct c985_poc *d)
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
    ret = CPR_MemoryWrite(d, 0, local_20);
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

        ret = CPR_MemoryWrite(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write row failed at local_1c=%u\n", local_1c);
            return ret;
        }
    }

    dev_vdbg(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = CPR_MemoryRead(d, 0, &local_24);
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
    ret = CPR_MemoryWrite(d, 0, local_18);
    if (ret) {
        dev_err(&d->pdev->dev, "MEMINT: CPR write #2 failed\n");
        return ret;
    }

    for (; local_18 > 1; local_18--) {
        u32 addr = 1 << ((local_1c + 0x15) & 0x1f);
        u32 val = local_18 - 1;

        dev_dbg(&d->pdev->dev, "Col loop: local_18=%u addr=0x%08x val=%u\n",
                local_18, addr, val);

        ret = CPR_MemoryWrite(d, addr, val);
        if (ret) {
            dev_err(&d->pdev->dev, "MEMINT: CPR write col failed at local_18=%u\n", local_18);
            return ret;
        }
    }

    dev_dbg(&d->pdev->dev, "Reading back from addr=0x0\n");
    ret = CPR_MemoryRead(d, 0, &local_24);
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
        CPR_MemoryWrite(d, 0, 0xAAAAAAAA);
        CPR_MemoryRead(d, 0, &test_val);
        dev_dbg(&d->pdev->dev,
                "CPR[0x0] = 0x%08x (expect 0xAAAAAAAA) %s\n",
                test_val, test_val == 0xAAAAAAAA ? "OK" : "FAIL");

        CPR_MemoryWrite(d, 0x1000, 0x55555555);
        CPR_MemoryRead(d, 0x1000, &test_val);
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
int CQLCodec_InitDevice(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;

    d = kzalloc(sizeof(*d), GFP_KERNEL);  // This already zeros v4l2_registered
    if (!d)
        return -ENOMEM;

    d->pdev = pdev;
    d->v4l2_registered = false;

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

    /* Initialize work queues EARLY (before ISR registration) */
    INIT_WORK(&d->irq_work, PciDpcForIsrArmMsg);
    INIT_WORK(&d->dma_work, PciDpcForIsr);
    init_completion(&d->mailbox_complete);
    init_completion(&d->dma_done);
    spin_lock_init(&d->irq_lock);
    spin_lock_init(&d->dma_lock);

    dev_dbg(&pdev->dev, "reg 0x00 = 0x%08x\n", readl(c985_bar1(d) + 0x00));

    /* Load default settings */
    cqlcodec_load_default_settings(d);

    /* Step 1: QPHCI_Init */
    ret = qphci_init(d);
    if (ret)
        goto err_out;

    /* Step 2: InitializeMemory */
    ret = CQLCodec_InitializeMemory(d);
    if (ret)
        goto err_out;

    /* Step 3: InitArmLoop */
    ret = qphci_init_arm_loop(d);
    if (ret)
        goto err_out;

    /* Step 4: SetGPIODefaults */
    gpio_set_defaults(d);

    /* Step 5: RegisterISR (CQLCodec_RegisterISR) */
    ret = CQLCodec_RegisterISR(d);
    if (ret) {
        dev_err(&pdev->dev, "CQLCodec_RegisterISR failed: %d\n", ret);
        goto err_out;
    }

    /* Step 6: Set callbacks (param_1[3].Close = param_2, param_1[3].Start = param_3) */
    d->codec.m_pDeviceCallback = DeviceCallbackFriend;
    d->codec.m_callbackContext = d;
    d->pcie.pBusCallbackFunc = (void *)DeviceCallbackFriend;
    d->pcie.pBusCallbackContext = d;

    /* Step 7: Enable interrupts */
    CPCIeCntl_EnableInterrupts(&d->codec.m_hci);

    /* Step 8: AOSwitch/VOSwitch */
    cqlcodec_ao_switch(d, d->codec.m_AOEnable);
    cqlcodec_vo_switch(d, d->codec.m_VOEnable);

    dev_info(&pdev->dev, "CQLCodec init complete\n");

    dev_info(&pdev->dev, "BAR0 phys=0x%llx virt=%p\n",
             (u64)pci_resource_start(pdev, 0),
             d->pcie.pRegisters);

    dev_info(&pdev->dev, "BAR1 phys=0x%llx virt=%p\n",
             (u64)pci_resource_start(pdev, C985_BAR_MMIO),
             d->pcie.pRegistersEx);

    dev_info(&pdev->dev, "pRegisters + 0x8000 = %p\n",
             (void *)((u8 *)d->pcie.pRegisters + 0x8000));

    dev_info(&pdev->dev, "Windows expects pRegistersEx = BAR0 + 0x8000\n");
    dev_info(&pdev->dev, "Your pRegistersEx = %p\n", d->pcie.pRegistersEx);

    dev_info(&pdev->dev, "TEST: BAR0+0x8030 = 0x%08x\n",
             readl(d->pcie.pRegisters + 0x8030));
    dev_info(&pdev->dev, "TEST: BAR1+0x0030 = 0x%08x\n",
             readl(d->pcie.pRegistersEx + 0x30));

    return 0;

    err_out:
    cancel_work_sync(&d->irq_work);
    cancel_work_sync(&d->dma_work);
    if (d->irq_registered) {
        CPCIeCntl_DisableInterrupts(&d->codec.m_hci);
        CPCIeCntl_UnregisterISR(d);
    }
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
 * Firmware download
 * --------------------------------------------------------------------- */
// CQLCodec_FWDownloadAll
int cqlcodec_fw_download(struct c985_poc *d, int do_reset)
{
    if (!do_reset) {
        return 0;  /* Nothing to do */
    }

    return CQLCodec_FWDownloadAll(d, 0, 1);
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

    return 0;
}

_EQPErrors CQLCodec_UpdateMiscConfig(struct cql_codec *codec, u32 task_id)
{
    struct task_data *td;
    _EQPErrors ret = QPERR_SUCCESS;

    /* Power state check */
    if (codec->m_PowerState != 0)
        return QPERR_SUCCESS;

    if (!codec->m_pTask || task_id >= 8)
        return QPERR_PARMS;

    td = &codec->m_pTask->m_TaskData[task_id];

    /* SystemLink if not acquired */
    if (!td->m_bAcquired) {
        ret = QPFWCODECAPI_SystemLink(codec, task_id,
                                      td->m_video_input, td->m_video_in_ch,
                                      td->m_video_output, td->m_video_out_ch,
                                      td->m_audio_input, td->m_audio_in_ch,
                                      td->m_audio_output, td->m_audio_out_ch);

        if (ret >= QPERR_SUCCESS)
            td->m_bAcquired = 1;
    }

    /* ChipType check: (ChipType & 6) == 0 */
    if ((codec->m_ChipType & 6) == 0) {
        /* SetEncMode: m_EncCapMode is u8[20], cast to extract u32s */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetEncMode(codec, task_id,
                                        *(u32 *)&td->m_EncCapMode[0],   /* capMode */
                                        *(u32 *)&td->m_EncCapMode[4],   /* trigMode */
                                        *(u32 *)&td->m_EncCapMode[8]);  /* gpioPin */
        }

        /* SetMJPEGQuality */
        if (ret >= QPERR_SUCCESS)
            ret = QPFWENCAPI_SetMJPEGQuality(codec, task_id, td->m_EncMjpegQuality);

        /* SetMJPEGFrameBuffer */
        if (ret >= QPERR_SUCCESS)
            ret = QPFWENCAPI_SetMJPEGFrameBuffer(codec, task_id, td->m_EncMjpegFrameBuffer);

        /* SetExternalTriggerToSync: m_ExternalTriggerToSync is u8[16] */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetExternalTriggerToSync(codec, task_id,
                                                      *(u32 *)&td->m_ExternalTriggerToSync[0],  /* Enable */
                                                      *(u32 *)&td->m_ExternalTriggerToSync[4]); /* gpioPin */
        }

        /* SetPTSResetByTrigger: m_PTSCounterReset is u8[20] */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetPTSResetByTrigger(codec, task_id,
                                                  *(u32 *)&td->m_PTSCounterReset[0],   /* Enable */
                                                  *(u32 *)&td->m_PTSCounterReset[4],   /* gpioPin */
                                                  *(u32 *)&td->m_PTSCounterReset[8]);  /* immediate */
        }

        /* SetRawVideoDecimation: m_RawVideoDecimationFactor is u8[20] */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetRawVideoDecimation(codec, task_id,
                                                   *(u32 *)&td->m_RawVideoDecimationFactor[0],   /* input_format */
                                                   *(u32 *)&td->m_RawVideoDecimationFactor[4],   /* output_format */
                                                   *(u32 *)&td->m_RawVideoDecimationFactor[8]);  /* scale_factor */
        }

        /* SetDeinterlaceMode */
        if (ret >= QPERR_SUCCESS)
            ret = QPFWENCAPI_SetDeinterlaceMode(codec, task_id, td->m_DeinterlaceMode);

        /* SetRateControlEx: m_rateControlEx is u32, extract bitfields */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetRateControlEx(codec, task_id,
                                              td->m_rateControlEx & 0xfff,
                                              (td->m_rateControlEx >> 12) & 1,
                                              (td->m_rateControlEx >> 13) & 0xffff);
        }

        /* SetLargeCompressBufferControl (conditional) */
        if ((td->m_LargeCompressBufferControl & 0x80000000) != 0) {
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_SetLargeCompressBufferControl(codec, task_id,
                                                               td->m_LargeCompressBufferControl);
            }
        }

        /* SetLowBitrateMode (conditional) */
        if (td->m_EnableLowBitrateMode != 0) {
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_SetLowBitrateMode(codec, task_id,
                                                   td->m_EnableLowBitrateMode);
            }
        }

        /* SetViuSyncCode */
        if (ret >= QPERR_SUCCESS) {
            ret = QPFWENCAPI_SetViuSyncCode(codec, task_id,
                                            codec->m_ulViuSyncCode1,
                                            codec->m_ulViuSyncCode2);
        }

        /* ChipType check: (ChipType & 0xe) == 0 */
        if ((codec->m_ChipType & 0xe) == 0) {
            /* SetIndexCapture */
            if (ret >= QPERR_SUCCESS) {
                if (td->pChannel[TASK_DATA_TYPE_INDEX] == NULL) {
                    ret = QPFWENCAPI_SetIndexCapture(codec, task_id, 0);
                } else {
                    ret = QPFWENCAPI_SetIndexCapture(codec, task_id,
                                                     td->m_EncIndexCapFreq);
                }
            }

            /* SetVBIInfo */
            /*
            if (ret >= QPERR_SUCCESS) {
                if (td->pChannel[TASK_DATA_TYPE_VBI] != NULL) {
                    struct c_vbi_out_channel *vbi_chan =
                    (struct c_vbi_out_channel *)td->pChannel[TASK_DATA_TYPE_VBI];
                    struct qp_vbi_dataformat vbi_fmt;
                    _EQPErrors vbi_ret = CVBIOutChannel_GetVBIFormat(vbi_chan, &vbi_fmt);

                    if (vbi_ret == QPERR_SUCCESS) {
                        ret = QPFWENCAPI_SetVBIInfo(codec, task_id, 1,
                                                    vbi_fmt.Top_StartLine,
                                                    (vbi_fmt.Top_EndLine - vbi_fmt.Top_StartLine) + 1,
                                                    vbi_fmt.Top_StartPixel,
                                                    vbi_fmt.Top_SamplesPerLine,
                                                    vbi_fmt.Bottom_StartLine,
                                                    (vbi_fmt.Bottom_EndLine - vbi_fmt.Bottom_StartLine) + 1,
                                                    vbi_fmt.Bottom_StartPixel,
                                                    vbi_fmt.Bottom_SamplesPerLine);
                    } else {
                        ret = QPFWENCAPI_SetVBIInfo(codec, task_id, 0, 0, 0, 0, 0, 0, 0, 0, 0);
                    }
                } else {
                    ret = QPFWENCAPI_SetVBIInfo(codec, task_id, 0, 0, 0, 0, 0, 0, 0, 0, 0);
                }
            } */

            /* SetMP4VideoBlockNumber */
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_SetMP4VideoBlockNumber(codec, task_id,
                                                        td->m_MP4VideoBlockNumber);
            }

            /* SetAudioEnhancement: m_audioEnhancement is u8[40], cast to extract u32s */
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_SetAudioEnhancement(codec, task_id,
                                                     *(u32 *)&td->m_audioEnhancement[0],   /* gain1 */
                                                     *(u32 *)&td->m_audioEnhancement[4],   /* gain2 */
                                                     *(u32 *)&td->m_audioEnhancement[8],   /* add */
                                                     *(u32 *)&td->m_audioEnhancement[12],  /* sub */
                                                     *(u32 *)&td->m_audioEnhancement[16],  /* att1 */
                                                     *(u32 *)&td->m_audioEnhancement[20],  /* att2 */
                                                     *(u32 *)&td->m_audioEnhancement[24],  /* lgain */
                                                     *(u32 *)&td->m_audioEnhancement[28]); /* rgain */
            }

            /* EnableVidPadding */
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_EnableVidPadding(codec, task_id,
                                                  td->m_bEncEnableVidPadding);
            }

            /* StillVideoInput */
            if (ret >= QPERR_SUCCESS) {
                ret = QPFWENCAPI_StillVideoInput(codec, task_id,
                                                 td->m_bEncVidStillInput);
            }

            /* Clear frozen flag */
            td->m_bEncVidFrozen = 0;
        }
    }

    return ret;
}

int CQLCodec_SetGPIOBitDirection(struct c985_poc *d, struct gpio_bit_direction *param_2)
{
    u32 local_10;

    if (param_2->bBitNumber >= 0x10) {
        dev_err(&d->pdev->dev,
                "CQLCodec_SetGPIOBitDirection() Invalid GPIO number(%d)\n",
                param_2->bBitNumber);
        return -EINVAL;
    }

    local_10 = readl(c985_bar1(d) + 0x610);
    local_10 = (local_10 & ~(1 << (param_2->bBitNumber & 0x1f))) |
    (param_2->Direction << (param_2->bBitNumber & 0x1f));
    writel(local_10, c985_bar1(d) + 0x610);
    wmb();

    return 0;
}

int CQLCodec_SetGPIOBitValue(struct c985_poc *d, struct gpio_bit_value *param_2)
{
    u32 local_10;

    if (param_2->bBitNumber >= 0x10) {
        dev_err(&d->pdev->dev,
                "CQLCodec_SetGPIOBitValue() Invalid GPIO number(%d)\n",
                param_2->bBitNumber);
        return -EINVAL;
    }

    local_10 = readl(c985_bar1(d) + 0x614);
    local_10 = (local_10 & ~(1 << (param_2->bBitNumber & 0x1f))) |
    (param_2->bValue << (param_2->bBitNumber & 0x1f));
    writel(local_10, c985_bar1(d) + 0x614);
    wmb();

    return 0;
}

int CQLCodec_UpdateEncoderConfig(struct c985_poc *d, u32 param_2, int param_3)
{
    struct c_task *pCVar1;
    struct task_data *td;
    int ret = 0;
    u32 uVar3;

    if (d->codec.m_PowerState != 0)
        return 0;

    dev_dbg(&d->pdev->dev, "CQLCodec_UpdateEncoderConfig()\n");

    pCVar1 = d->codec.m_pTask;
    if (!pCVar1) {
        dev_err(&d->pdev->dev,
                "CQLCodec_UpdateEncoderConfig() m_pTask is NULL\n");
        return -EINVAL;
    }

    if (param_2 >= MAX_TASKS) {
        dev_err(&d->pdev->dev,
                "CQLCodec_UpdateEncoderConfig() invalid taskId %u\n", param_2);
        return -EINVAL;
    }

    if (param_3 != 0) {
        ret = QPFWAPI_MailboxReady(d, 500);
        if (ret < 0) {
            dev_err(&d->pdev->dev,
                    "CQLCodec_UpdateEncoderConfig() QPFWAPI_MailboxReady() failed(%d)\n",
                    ret);
            return ret;
        }
    }

    pCVar1 = d->codec.m_pTask;
    td = &pCVar1->m_TaskData[param_2];

    ret = QPFWENCAPI_SetSystemControl(d, td->m_systemControl);
    if (ret >= 0)
        ret = QPFWENCAPI_SetPictureResolution(d, td->m_pictureResolution);
    if (ret >= 0)
        ret = QPFWENCAPI_SetInputControl(d, td->m_inputControl);
    if (ret >= 0)
        ret = QPFWENCAPI_SetRateControl(d, td->m_rateControl);
    if (ret >= 0)
        ret = QPFWENCAPI_SetVBRBitRate(d, td->m_bitRate);
    if (ret >= 0)
        ret = QPFWENCAPI_SetFilterControl(d, td->m_filterControl);
    if (ret >= 0)
        ret = QPFWENCAPI_SetGOPLoopFilter(d, td->m_gopLoopFilter);
    if (ret >= 0)
        ret = QPFWENCAPI_SetETControl(d, td->m_etControl);
    if (ret >= 0)
        ret = QPFWENCAPI_SetBlockSize(d, td->m_blkXferSize);
    if (ret >= 0)
        ret = QPFWENCAPI_SetOutPictureResolution(d, td->m_outPictureResolution);
    if (ret >= 0)
        ret = QPFWENCAPI_SetAudioControlParameters(d, td->m_audioControlParam);

    uVar3 = td->m_audioControlParam >> 28;

    switch (uVar3) {
        case 0:
        case 9:
            /* LPCM */
            td->m_audioControlExLPCM &= 0xffffff;
            if (ret >= 0)
                ret = QPFWENCAPI_SetAudioControlExtension(d, td->m_audioControlExLPCM);
        break;

        case 8:
        case 10:
            /* G711 */
            if (ret >= 0)
                ret = QPFWENCAPI_SetAudioControlExtension(d, td->m_audioControlExG711);
        break;

        case 11:
            /* SILK */
            if (ret >= 0)
                ret = QPFWENCAPI_SetAudioControlExtension(d, td->m_audioControlExSILK);
        break;

        default:
            /* AAC */
            if (ret >= 0)
                ret = QPFWENCAPI_SetAudioControlExtension(d, td->m_audioControlExAAC);
        break;
    }

    if (param_3 != 0) {
        if (ret >= 0)
            ret = QPFWENCAPI_UpdateConfig(d, param_2);
        QPFWAPI_MailboxDone(d);
    }

    dev_dbg(&d->pdev->dev, "CQLCodec_UpdateEncoderConfig() Status(0x%x)\n", ret);

    return ret;
}

struct c985_poc *CQLCodec_Constructor(struct c985_poc *d, struct c_object *param_2,
                                      u32 param_3, void *param_4, void *param_5)
{

    /* Allocate and initialize event groups */
    d->codec.m_EvtTask = kzalloc(sizeof(struct t_event_block), GFP_KERNEL);
    if (!d->codec.m_EvtTask) {
        dev_err(&d->pdev->dev, "Failed to allocate m_EvtTask\n");
        CQLCodec_Destructor(d);
        return NULL;
    }
    QPOSMCreateEvtgrp(d->codec.m_EvtTask);

    d->codec.m_EvtTaskReply = kzalloc(sizeof(struct t_event_block), GFP_KERNEL);
    if (!d->codec.m_EvtTaskReply) {
        dev_err(&d->pdev->dev, "Failed to allocate m_EvtTaskReply\n");
        CQLCodec_Destructor(d);
        return NULL;
    }
    QPOSMCreateEvtgrp(d->codec.m_EvtTaskReply);

    d->codec.m_EvtSyncDma = kzalloc(sizeof(struct t_event_block), GFP_KERNEL);
    if (!d->codec.m_EvtSyncDma) {
        dev_err(&d->pdev->dev, "Failed to allocate m_EvtSyncDma\n");
        CQLCodec_Destructor(d);
        return NULL;
    }
    QPOSMCreateEvtgrp(d->codec.m_EvtSyncDma);

    struct c_task *pCVar2;

    dev_dbg(&d->pdev->dev, "CQLCodec_Constructor()\n");

    if (d == NULL)
        return NULL;

    CObject_Constructor(&d->codec.m_Object, param_2, param_3);

    /* Initialize member variables */
    d->codec.m_bHCIInited = 0;
    d->codec.m_pUsbCntl = param_4;
    d->codec.m_pPCIeCntl = param_5;
    d->codec.m_pTask = NULL;
    d->codec.m_pVideoFW = NULL;
    d->codec.m_pAudioFW = NULL;
    d->codec.m_QL201FWSize = 0;
    d->codec.m_QL201AudFWSize = 0;
    d->codec.m_bVideoFWUpdated = 0;
    d->codec.m_bAudioFWUpdated = 0;
    d->codec.m_bInterruptAttached = 0;
    d->codec.m_interruptNumber = 0;
    d->codec.m_GPIODirections = 0;
    d->codec.m_GPIOValues = 0;
    d->codec.m_PowerState = 0;
    d->codec.m_ErrorRecovery = 1;

    /* Create event groups (wait queues in Linux) */
    init_waitqueue_head(&d->evt_task);
    d->evt_task_flags = 0;

    init_waitqueue_head(&d->evt_task_reply);
    d->evt_task_reply_flags = 0;

    init_waitqueue_head(&d->evt_sync_dma);
    d->evt_sync_dma_flags = 0;

    /* Allocate and construct CTask */
    pCVar2 = kzalloc(sizeof(struct c_task), GFP_KERNEL);
    if (pCVar2 == NULL) {
        dev_err(&d->pdev->dev,
                "CQLCodec_Constructor() kzalloc for CTask Failed!!!!\n");
        CQLCodec_Destructor(d);
        return NULL;
    }
    d->codec.m_pTask = pCVar2;

    pCVar2 = CTask_Constructor(d->codec.m_pTask, &d->codec.m_Object, 2, 0x10, &d->codec,
                               d->codec.m_EvtTask, d->codec.m_EvtTaskReply);
    if (pCVar2 == NULL) {
        dev_err(&d->pdev->dev,
                "CQLCodec_Constructor() CTask_Constructor Failed!!!!\n");
        CQLCodec_Destructor(d);
        return NULL;
    }

    /* Mutex for FWAPI */
    mutex_init(&d->mailbox_lock);

    return d;
}


int CQLCodecLib_CreatePeripherals(struct cql_codec_lib *param_1)
{
pr_debug ("CQLCodecLib_CreatePeripherals() STUBBED");

    return 1;
}

int CQLCodec_AllocEncodeTask(struct c985_poc *d, u32 *param_2)
{
    int ret;

    if (param_2 == NULL)
        return -EINVAL;

    ret = ((int (*)(struct c_task *, enum task_type, void *))d->codec.m_pTask->Alloc)(d->codec.m_pTask, TASK_TYPE_ENC, param_2);
    dev_dbg(&d->pdev->dev,
            "CQLCodec_AllocEncodeTask() qpStatus(%d) phTask(%d)\n",
            ret, *param_2);

    return ret;
}

int CQLCodec_ReleaseTask(struct c985_poc *d, u32 param_2)
{
    struct c_task *task;

    task = d->codec.m_pTask;
    if (task == NULL) {
        dev_err(&d->pdev->dev, "CQLCodec_ReleaseTask() no task structure\n");
        return -ENODEV;
    }

    ((void (*)(struct c_task *, u32))task->Release)(task, param_2);

    dev_dbg(&d->pdev->dev, "CQLCodec_ReleaseTask() hTask(%d)\n", param_2);

    return 0;
}

int CQLCodec_IsCodecIdle(struct c985_poc *d)
{
    enum task_state state;
    u32 i;

    for (i = 0; i < MAX_TASKS; i++) {
        state = CTask_GetTaskState(d->codec.m_pTask, i);
        if (state != TASK_STATE_IDLE)
            return 0;
    }

    return 1;
}

int CQLCodec_AddBuffer(struct c985_poc *d, u32 param_2,
                       struct qp_buffer_descriptor *param_3)
{
    dev_info(&d->pdev->dev, "AddBuffer: stream=%u buf=%px size=%u\n",
             param_2, param_3->pBuffer, param_3->ulBufferSize);

    /* TODO: Add to queue, setup DMA */
    return 0;
}

_EQPErrors CQLCodec_Open(struct i_mpeg_codec *codec_iface,
                         u32 task_id,
                         u32 channel_type,
                         void *param_4,
                         u32 *channel_handle,
                         void *callback,
                         void *callback_ctx)
{
    struct cql_codec *codec;
    struct c_task *task;
    struct c_channel *channel = NULL;
    u32 handle;
    _EQPErrors ret;

    /* Get parent cql_codec from i_mpeg_codec */
    codec = container_of(codec_iface, struct cql_codec, m_iMpegCodec);
    task = codec->m_pTask;

    /* Validate parameters */
    if (!channel_handle || !task || task_id >= 8)
        return QPERR_PARMS;

    /* Check task validity - matches decomp check at param_1[3].Stop + task_id*0x64C + 0x194 */
    if (!task->m_TaskData[task_id].valid)
        return QPERR_PARMS;

    /* Allocate and construct channel based on type */
    switch (channel_type) {
        case 0:  /* MPEG Out */
            channel = kzalloc(0x1190, GFP_KERNEL);
            if (!channel)
                return QPERR_NOMEM;
        CMPEGOutChannel_Constructor(channel, &codec->m_Object, 2, task_id, task);
        break;

        case 4:  /* YUV In */
            channel = kzalloc(0x11a8, GFP_KERNEL);
            if (!channel)
                return QPERR_NOMEM;
        CYUVInChannel_Constructor(channel, &codec->m_Object, 2, task_id, task);
        break;

        case 5:  /* PCM In */
            channel = kzalloc(0x1190, GFP_KERNEL);
            if (!channel)
                return QPERR_NOMEM;
        CPCMInChannel_Constructor(channel, &codec->m_Object, 2, task_id, task);
        break;

        /* Add other cases as needed matching decompilation */

        default:
            return QPERR_PARMS;
    }

    if (!channel)
        return QPERR_NOMEM;

    /* Register with object manager to get handle */
    handle = CObjectMgr_AddObject(&codec->m_Object, channel);
    if (!handle) {
        kfree(channel);
        return QPERR_NOMEM;
    }

    /* Initialize channel fields - NOTE: pChannel is NOT set here */
    channel->m_hTask = task_id;
    channel->m_hChannel = handle;
    channel->m_dataType = channel_type;
    channel->m_pTask = task;
    channel->m_pDeviceCallback = callback;
    channel->m_callbackContext = callback_ctx;

    /* Call channel's Open method */
    if (channel->Open) {
        ret = ((_EQPErrors (*)(struct c_channel *, u32, u32, void *, void *, void *))
        channel->Open)(channel, handle, channel_type, param_4, callback, callback_ctx);

        /* MATCHES DECOMP: if (extraout_var < 0) CQLCodec_Close(param_1, uVar2); */
        if (ret < 0) {
            CQLCodec_Close(codec_iface, handle);
            return ret;
        }
    }

    /* Success - store handle */
    *channel_handle = handle;

    pr_debug("CQLCodec_Open: type=%u task_id=%u handle=%u\n",
             channel_type, task_id, handle);

    return QPERR_SUCCESS;
}
int CQLCodecLibBusCallback(void *context, u32 event_type, void *param)
{
    struct c985_poc *d = (struct c985_poc *)context;

    dev_dbg(&d->pdev->dev, "CQLCodecLibBusCallback() event_type=%u\n",
            event_type);

    if (!d || !d->codec.m_EvtTask)
        return QPERR_SUCCESS;

    switch (event_type) {
        case 1:
            /* ARM message */
            dev_dbg(&d->pdev->dev, "CQLCodecLibBusCallback() ARM message\n");
            QPOSMSetEvtgrp(d->codec.m_EvtTask, 0x04);
            break;

        case 2:
            /* DMA read complete */
            dev_dbg(&d->pdev->dev, "CQLCodecLibBusCallback() DMA read complete\n");
            QPOSMSetEvtgrp(d->codec.m_EvtTask, 0x08);
            QPOSMSetEvtgrp(d->codec.m_EvtSyncDma, 0x01);
            break;

        case 4:
            /* DMA write complete */
            dev_dbg(&d->pdev->dev, "CQLCodecLibBusCallback() DMA write complete\n");
            QPOSMSetEvtgrp(d->codec.m_EvtTask, 0x10);
            QPOSMSetEvtgrp(d->codec.m_EvtSyncDma, 0x02);
            break;

        default:
            dev_dbg(&d->pdev->dev, "CQLCodecLibBusCallback() unknown event %u\n",
                    event_type);
            break;
    }

    return QPERR_SUCCESS;
}

_EQPErrors CQLCodec_Close(struct i_mpeg_codec *iface, u32 handle)
{
    struct cql_codec *codec;
    struct c985_poc *poc;
    struct c_channel *channel;
    _EQPErrors ret;

    codec = container_of(iface, struct cql_codec, m_iMpegCodec);
    poc = container_of(codec, struct c985_poc, codec);

    dev_dbg(&poc->pdev->dev, "CQLCodec_Close() hStream(%u)\n", handle);

    /* Use m_pChannelMgr and cast return value */
    channel = (struct c_channel *)CObjectMgr_RemoveObject(codec->m_pChannelMgr, handle);

    if (!channel) {
        return QPERR_PARMS;
    }

    /* FIX: Cast Close from void* to function pointer before calling */
    if (channel->Close) {
        ret = ((_EQPErrors (*)(struct c_channel *))channel->Close)(channel);
    } else {
        ret = QPERR_SUCCESS;
    }

    /* Destructor and free */
    CChannel_Destructor(channel);
    kfree(channel);

    return ret;
}
int CQLCodec_RegisterISR(struct c985_poc *d)
{
    int ret;

    d->codec.m_bInterruptAttached = 0;

    if (d->codec.m_hci.m_bus_type == QPHCI_BUS_PCI) {
        ret = CPCIeCntl_RegisterISR(d);
        d->codec.m_bInterruptAttached = (ret >= 0) ? 1 : 0;
    } else {
        /* USB/emulation not supported */
        ret = -ENODEV;
    }

    return ret;
}
int CQLCodecLib_CreateInitUSB(struct cql_codec_lib *param_1, u32 p0, u32 p1, u32 p2, u32 p3, u32 mode)
{
    pr_debug ("CQLCodecLib_CreateInitUSB STUB");
    return 1;
}

int CQLCodecLib_InitPCIe(struct cql_codec_lib *param_1)
{
    pr_debug ("CQLCodecLib_CreatePCIe STUB");
    return 1;
}

int CQLCodecLib_CreatePCIe(struct cql_codec_lib *param_1)
{
    pr_debug ("CQLCodecLib_CreatePCIe STUB");
    return 1;
}

