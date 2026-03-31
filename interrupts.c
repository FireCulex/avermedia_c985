#include <linux/interrupt.h>
#include <linux/pci.h>
#include "avermedia_c985.h"
#include "interrupts.h"

/*
 * pci_interrupt_service
 *
 * Direct port of PciInterruptService from decompile.
 *
 * Original checks:
 *   1. pRegistersEx + 0x30 for bit 30 (0x40000000)
 *   2. Loop through DMA channels, check ControlStatus at (channel * 0x100) + 0x04
 */
static irqreturn_t pci_interrupt_service(int irq, void *dev_id)
{
    struct c985_poc *d = dev_id;
    u32 reg;
    int i;
    irqreturn_t ret = IRQ_NONE;

    dev_info(&d->pdev->dev, "IRQ: num_dma_channels=%d\n", d->num_dma_channels);

    for (i = 0; i < d->num_dma_channels; i++) {
        void __iomem *channel_base = d->bar0 + (i * PED_DMA_ENGINE_SIZE);
        reg = readl(channel_base + PED_DMA_ENGINE_CONTROL_STATUS);

        dev_info(&d->pdev->dev, "IRQ: ch%d reg=0x%08x\n", i, reg);

        if ((reg & PED_INT_PENDING) && (reg & PED_INT_ENABLED)) {
            writel(PED_INT_CLEAR, channel_base + PED_DMA_ENGINE_CONTROL_STATUS);
            dev_info(&d->pdev->dev, "IRQ: DMA channel %d CLEARED\n", i);
            ret = IRQ_HANDLED;
        }
    }

    return ret;
}

/*
 * cpciectl_enable_interrupts
 *
 * Direct port of CPCIeCntl_EnableInterrupts from decompile.
 *
 * Original:
 *   *(pRegisters + 0x4000) |= 0x01;
 */
void cpciectl_enable_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
    val |= PED_GLOBAL_INT_ENABLE;
    writel(val, d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
}

/*
 * cpciectl_disable_interrupts
 *
 * Direct port of CPCIeCntl_DisableInterrupt from decompile.
 *
 * Original:
 *   *(pRegisters + 0x4000) &= 0xFFFFFFFE;
 */
void cpciectl_disable_interrupts(struct c985_poc *d)
{
    u32 val;

    val = readl(d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
    val &= PED_GLOBAL_INT_DISABLE_MASK;
    writel(val, d->bar0 + PED_DMA_COMMON_CONTROL_STATUS);
}

/*
 * pci_interrupt_service_register
 *
 * Port of CPCIeCntl_RegisterISR from decompile.
 */
int pci_interrupt_service_register(struct c985_poc *d)
{
    int ret;

    ret = request_irq(d->pdev->irq, pci_interrupt_service,
                      IRQF_SHARED, DRV_NAME, d);
    if (ret) {
        d->irq_registered = 0;
        return ret;
    }

    d->irq_registered = 1;
    return 0;
}

/*
 * pci_interrupt_service_unregister
 */
void pci_interrupt_service_unregister(struct c985_poc *d)
{
    if (d->irq_registered) {
        free_irq(d->pdev->irq, d);
        d->irq_registered = 0;
    }
}
