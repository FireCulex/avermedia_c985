// SPDX-License-Identifier: GPL-2.0
/*
 * interrupts.c - Interrupt handling for AVerMedia C985
 *
 * Based on PciInterruptService from decompiled Windows driver.
 *
 * Key insight from decompile:
 *   - pRegisters (BAR0) at offset 0x1758 in device extension
 *   - pRegistersEx (BAR1) at offset 0x1760 in device extension
 *   - DmaDevices[i].DmaIndex gives actual engine number (0 or 32)
 *   - Engine base = pRegisters + DmaIndex * 0x100
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include "avermedia_c985.h"
#include "interrupts.h"

/*
 * pci_interrupt_service - Main ISR
 *
 * From PciInterruptService decompile:
 *
 * 1. Check pRegistersEx + 0x30 for bit 30 (HCI interrupt)
 *    if ((*(pRegistersEx + 0x30) & 0x40000000) != 0) {
 *        *(pRegistersEx + 0x30) = 0x40000000;  // clear
 *        KeInsertQueueDpc(param_2 + 0xb8, ...);
 *    }
 *
 * 2. Loop through DMA channels:
 *    for (i = 0; i < m_NumDmaAvailable; i++) {
 *        engine_base = DmaDevices[i].DmaIndex * 0x100 + pRegisters;
 *        status = *(engine_base + 4);
 *        if ((status & 2) && (status & 1)) {
 *            *(engine_base + 4) = 3;  // acknowledge
 *            InterruptStatus |= (1 << i);
 *        }
 *    }
 *
 * 3. If any DMA completed:
 *    KeInsertQueueDpc(FunctionalDeviceObject->Dpc + 200, ...);
 */
static irqreturn_t pci_interrupt_service(int irq, void *dev_id)
{
    struct c985_poc *d = dev_id;
    u32 hci_status;
    u32 dma_status;
    int i;
    irqreturn_t ret = IRQ_NONE;
    bool dma_completed = false;

    /*
     * Check BAR1 + 0x30 bit 30 for HCI/ARM interrupt
     */
    if (d->bar1) {
        hci_status = readl(d->bar1 + HCI_INT_STATUS_REG);
        if (hci_status & HCI_INT_STATUS_BIT) {
            /* Acknowledge by writing the bit back */
            writel(HCI_INT_STATUS_BIT, d->bar1 + HCI_INT_STATUS_REG);

            dev_dbg(&d->pdev->dev, "IRQ: HCI interrupt\n");

            /* Schedule HCI work */
            schedule_work(&d->irq_work);
            ret = IRQ_HANDLED;
        }
    }

    /*
     * Check each discovered DMA engine
     *
     * Critical: use dma_engine_idx[i] (the actual engine index like 0 or 32),
     * not i directly. The Windows driver does:
     *   DmaDevices[i].DmaIndex * 0x100 + pRegisters
     */
    for (i = 0; i < d->num_dma_channels; i++) {
        u32 engine_idx = d->dma_engine_idx[i];
        void __iomem *engine_ctrl = d->bar0 +
        (engine_idx * PED_DMA_ENGINE_SIZE) +
        PED_DMA_ENGINE_CONTROL_STATUS;

        dma_status = readl(engine_ctrl);

        /* Check: (status & 1) && (status & 2) */
        if ((dma_status & PED_DMA_CTRL_RUNNING) &&
            (dma_status & PED_DMA_CTRL_INT_PENDING)) {

            /* Acknowledge: write 0x03 */
            writel(PED_DMA_CTRL_ACK, engine_ctrl);

        /* Set bit in interrupt status bitmask */
        d->dma_interrupt_status |= (1 << i);

        dev_dbg(&d->pdev->dev, "IRQ: DMA channel %d (engine %d) complete\n",
                i, engine_idx);

        dma_completed = true;
        ret = IRQ_HANDLED;
            }
    }

    /* If any DMA completed, schedule work */
    if (dma_completed) {
        schedule_work(&d->dma_work);
    }

    return ret;
}

/*
 * cpciectl_enable_interrupts - Enable global DMA interrupts
 *
 * From CPCIeCntl_EnableInterrupts:
 *   *(pRegisters + 0x4000) |= 0x01
 */
void cpciectl_enable_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
    val |= PED_GLOBAL_INT_ENABLE;
    writel(val, d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);

    dev_dbg(&d->pdev->dev, "Interrupts enabled: 0x4000=0x%08x\n",
            readl(d->bar0 + PED_DMA_COMMON_CONTROL_STATUS));
}

/*
 * cpciectl_disable_interrupts - Disable global DMA interrupts
 *
 * From decompile:
 *   *(pRegisters + 0x4000) &= 0xFFFFFFFE
 */
void cpciectl_disable_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
    val &= PED_GLOBAL_INT_DISABLE_MASK;
    writel(val, d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
}

/*
 * pci_interrupt_service_register - Register IRQ handler
 */
int pci_interrupt_service_register(struct c985_poc *d)
{
    int ret;

    /* Initialize interrupt status */
    d->dma_interrupt_status = 0;

    ret = request_irq(d->pdev->irq, pci_interrupt_service,
                      IRQF_SHARED, DRV_NAME, d);
    if (ret) {
        dev_err(&d->pdev->dev, "Failed to register IRQ %d: %d\n",
                d->pdev->irq, ret);
        d->irq_registered = false;
        return ret;
    }

    d->irq_registered = true;
    dev_info(&d->pdev->dev, "IRQ %d registered\n", d->pdev->irq);
    return 0;
}

/*
 * pci_interrupt_service_unregister - Unregister IRQ handler
 */
void pci_interrupt_service_unregister(struct c985_poc *d)
{
    if (d->irq_registered) {
        cpciectl_disable_interrupts(d);
        free_irq(d->pdev->irq, d);
        d->irq_registered = false;
        dev_info(&d->pdev->dev, "IRQ unregistered\n");
    }
}
