#include <linux/interrupt.h>
#include <linux/pci.h>
#include "structs.h"
#include "avermedia_c985.h"
#include "interrupts.h"



void CPCIeCntl_DisableInterrupts(struct ihciapi *hci)
{
    struct cql_codec *codec = hci->m_pMpegCodec;
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    u32 val;

    if (!d || !d->pcie.pRegisters)
        return;

    val = readl(c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
    val &= PED_GLOBAL_INT_DISABLE_MASK;
    writel(val, c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
}

void CPCIeCntl_EnableInterrupts(struct ihciapi *hci)
{
    struct cql_codec *codec = hci->m_pMpegCodec;
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    u32 val;

    if (!d || !d->pcie.pRegisters)
        return;

    val = readl(c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
    val |= PED_GLOBAL_INT_ENABLE;
    writel(val, c985_bar0(d) + PED_DMA_COMMON_CONTROL_STATUS);
}
// value 0x8030 comes from PciValidateConfig

irqreturn_t PciInterruptService(int irq, void *dev_id)
{
    struct c985_poc *d = dev_id;
    u32 status;
    void __iomem *engine_regs;
    irqreturn_t ret = IRQ_NONE;
    bool dma_complete = false;
    int i;

    u32 bar1_0x30, bar1_0x800, bar1_0x804;
    u32 dma0_status, dma1_status;
    static int print_count = 0;

    bar1_0x30 = readl(c985_bar1(d) + 0x30);
    bar1_0x800 = readl(c985_bar1(d) + 0x800);
    bar1_0x804 = readl(c985_bar1(d) + 0x804);
    dma0_status = readl(c985_bar0(d) + 0x04);
    dma1_status = readl(c985_bar0(d) + 0x2004);

    if (print_count < 20) {
        dev_info(&d->pdev->dev,
                 "ISR: 0x30=0x%08x 0x800=0x%08x 0x804=0x%08x dma0=0x%08x dma1=0x%08x\n",
                 bar1_0x30, bar1_0x800, bar1_0x804, dma0_status, dma1_status);
        print_count++;
    }


    dev_dbg(&d->pdev->dev,
            "II$$$$$$ PciInterruptService Interrupt=%d DeviceExtension=%p\n",
            irq, d);

    /* Check BAR1 register 0x30 for bit 30 (ARM message interrupt) */
    if (c985_bar0(d) != NULL) {
        status = readl(c985_bar0(d) + 0x8030);
        if (status & 0x40000000) {
            writel(0x40000000, c985_bar0(d) + 0x8030);
            ret = IRQ_HANDLED;
            schedule_work(&d->irq_work);
        }
    }

    /* Loop through available DMA channels */
    for (i = 0; i < d->pcie.m_NumDmaAvailable; i++) {
        /* Get engine registers using DmaIndex from DmaDevices array */
        engine_regs = c985_bar0(d) + (d->pcie.DmaDevices[i].DmaIndex * 0x100);
        status = readl(engine_regs + 4);

        /* Check if both bit 0 and bit 1 are set (completion + enabled) */
        if ((status & 0x02) && (status & 0x01)) {
            /* Acknowledge by writing back bits 0 and 1 */
            writel(0x03, engine_regs + 4);

            /* Set interrupt status bit for this channel */
            d->pcie.InterruptStatus |= (1 << (i & 0x1f));

            ret = IRQ_HANDLED;
            dma_complete = true;
        }
    }

    if (dma_complete) {
        schedule_work(&d->dma_work);
    }

    dev_info_ratelimited(&d->pdev->dev, "ISR returning %d\n", ret);
    return ret;
}

void PciDpcForIsrArmMsg(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, irq_work);

    dev_dbg(&d->pdev->dev, "PciDpcForIsrArmMsg()\n");

    if (d->pcie.pBusCallbackFunc) {
        ((void (*)(void *, u32, void *))d->pcie.pBusCallbackFunc)(d->pcie.pBusCallbackContext, 1, NULL);
    }
}

void PciDpcForIsr(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, dma_work);

    dev_dbg(&d->pdev->dev, "PciDpcForIsr(0x%p)\n", d);

    PedDmaDpcHandler(d);
}


void PedDmaRequestFree(struct pci_dma_request *req)
{
    struct pci_dma_buffer *pPVar1;
    unsigned long flags;

    if (req == NULL)
        return;

    pPVar1 = req->pDmaBuffer;
    if (pPVar1 != NULL) {
        spin_lock_irqsave(&pPVar1->DmaRequestListLock, flags);
        list_del(&req->ListEntry);
        spin_unlock_irqrestore(&pPVar1->DmaRequestListLock, flags);
    }

    /* Call request callback if set */
    if (req->pRequestCB != NULL) {
        ((void (*)(struct pci_dma_request *))req->pRequestCB)(req);
        req->pRequestCB = NULL;
    }

    kfree(req);
}

/**
 * DM_ClearInterrupt - Clear specific interrupt types in BAR1 register 0x800
 * @d: device structure
 * @int_type: bitmask of interrupt types to clear
 *            0x01 = ARM message  -> clears bit 16
 *            0x02 = DMA write    -> clears bit 18
 *            0x04 = DMA read     -> clears bit 17
 *
 * From decompile (DM_ClearInterrupt):
 *   Reads 0x800, modifies bits 16-18, writes back
 *
 * Returns: 0 on success
 */
int DM_ClearInterrupt(struct c985_poc *d, u32 int_type)
{
    u32 reg_800;
    u32 bit16, bit17, bit18;

    reg_800 = readl(c985_bar1(d)  + 0x800);

    /* Map status bits to hardware bits */
    bit16 = (int_type & DM_INT_ARM_MSG)   ? 1 : 0;  /* 0x01 -> bit 16 */
    bit17 = (int_type & DM_INT_DMA_READ)  ? 1 : 0;  /* 0x04 -> bit 17 */
    bit18 = (int_type & DM_INT_DMA_WRITE) ? 1 : 0;  /* 0x02 -> bit 18 */

    /* Clear bits 16-18, then set the ones we want to clear (write-1-to-clear?) */
    reg_800 = (reg_800 & 0xFFF8FFFF) |
    (bit16 << 16) |
    (bit17 << 17) |
    (bit18 << 18);

    if (reg_800 != 0)
        writel(reg_800, c985_bar1(d)  + 0x800);

    return 0;
}
int CDevice_DeviceCallback(void *context, u32 event_type, void *param_2)
{
    /* Windows stub - does nothing */
    return 0;
}

int DeviceCallbackFriend(void *context, u32 event_type, void *param_2)
{
    return CDevice_DeviceCallback(context, event_type, param_2);
}
