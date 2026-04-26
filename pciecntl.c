// SPDX-License-Identifier: GPL-2.0
/*
 * pciecntl.c - PCIe DMA controller for AVerMedia C985
 *
 * DMA engine uses descriptor-based scatter-gather transfers.
 * Each descriptor is 32 bytes (struct ped_dma_descriptor) and
 * forms a linked list via NextDescriptor field.
 *
 * The C985 has at least 2 DMA engines:
 *   Engine 0 @ BAR1+0x810: Card-to-System (read from card)
 *   Engine 1 @ BAR1+0x830: System-to-Card (write to card)
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "avermedia_c985.h"
#include "pciecntl.h"
#include "interrupts.h"

/* DMA timeout in milliseconds */
#define DMA_TIMEOUT_MS          2000

/**
 * pciecntl_init - Initialize PCIe DMA controller
 * @d: device context
 *
 * Sets up DMA masks and initializes synchronization primitives.
 */
int pciecntl_init(struct c985_poc *d)
{
    int ret;

    /* Try 64-bit DMA first */
    ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        /* Fall back to 32-bit */
        ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(32));
        if (ret) {
            dev_err(&d->pdev->dev, "Failed to set DMA mask\n");
            return ret;
        }
        dev_info(&d->pdev->dev, "DMA: using 32-bit addressing\n");
    } else {
        dev_info(&d->pdev->dev, "DMA: using 64-bit addressing\n");
    }

    spin_lock_init(&d->dma_lock);
    init_completion(&d->dma_done);
    d->dma_active = false;

    return 0;
}

/**
 * pciecntl_cleanup - Cleanup DMA controller
 * @d: device context
 */
void pciecntl_cleanup(struct c985_poc *d)
{
    unsigned long flags;

    spin_lock_irqsave(&d->dma_lock, flags);
    if (d->dma_active) {
        dev_warn(&d->pdev->dev, "DMA still active during cleanup\n");
        /* Could try to stop the engine here */
    }
    spin_unlock_irqrestore(&d->dma_lock, flags);
}

/**
 * pciecntl_build_descriptor - Build a DMA descriptor
 * @d:          device context
 * @card_addr:  source/dest address in card RAM (byte address)
 * @host_phys:  host physical address
 * @length:     transfer length in bytes
 * @is_last:    true if this is the last descriptor in chain
 * @is_write:   true if writing to card (S2C), false if reading (C2S)
 * @desc:       output descriptor structure
 *
 * Builds one descriptor for the DMA chain.
 */
static void pciecntl_build_descriptor(struct c985_poc *d,
                                      u64 card_addr,
                                      u64 host_phys,
                                      u32 length,
                                      bool is_last,
                                      bool is_write,
                                      struct ped_dma_descriptor *desc)
{
    u64 card_addr_ex;

    memset(desc, 0, sizeof(*desc));

    /*
     * Control word:
     *   Mode 3 (0x0804200F) for compressed video transfers
     *   Mode 0 (0x0000000F) for basic transfers
     */
    desc->Control = DESC_CTRL_MODE_3;

    /* Transfer length */
    desc->ByteCount = length;

    /* Host physical address */
    desc->SystemAddress = host_phys;

    /*
     * Card address with extended flags:
     *   - Bits [31:0]  = card byte address
     *   - Bit 34       = mode flag (set for special modes)
     *   - Bit 59       = last descriptor flag (for reads)
     */
    card_addr_ex = card_addr | CARD_ADDR_MODE_FLAG;

    if (is_last && !is_write)
        card_addr_ex |= CARD_ADDR_LAST_DESC;

    desc->CardAddress = card_addr_ex;

    /* Next descriptor (0 = end of chain) */
    desc->NextDescriptor = 0;

    dev_dbg(&d->pdev->dev,
            "DMA desc: ctrl=0x%08x len=%u sys=0x%llx card=0x%llx\n",
            desc->Control, desc->ByteCount,
            desc->SystemAddress, desc->CardAddress);
}

/**
 * pciecntl_start_dma_read - Start DMA read from card to host
 * @d:          device context
 * @card_addr:  source address in card RAM (byte address)
 * @host_buf:   destination buffer (kernel virtual address)
 * @length:     transfer length in bytes
 * @sync:       if true, wait for completion before returning
 *
 * Returns 0 on success, negative error code on failure.
 */
int pciecntl_start_dma_read(struct c985_poc *d,
                            u32 card_addr,
                            void *host_buf,
                            u32 length,
                            bool sync)
{
    struct ped_dma_descriptor *desc_virt;
    dma_addr_t desc_phys, host_phys;
    void __iomem *engine_base;
    u32 status;
    unsigned long flags;
    int ret = 0;

    if (!host_buf || length == 0)
        return -EINVAL;

    dev_dbg(&d->pdev->dev,
            "DMA read: card=0x%08x len=%u sync=%d\n",
            card_addr, length, sync);

    engine_base = pciecntl_engine_base(d, 0);

    /* Allocate descriptor (must be 32-byte aligned, coherent) */
    desc_virt = dma_alloc_coherent(&d->pdev->dev, sizeof(*desc_virt),
                                   &desc_phys, GFP_KERNEL);
    if (!desc_virt)
        return -ENOMEM;

    /* Map host buffer for DMA (card writes to host) */
    host_phys = dma_map_single(&d->pdev->dev, host_buf, length,
                               DMA_FROM_DEVICE);
    if (dma_mapping_error(&d->pdev->dev, host_phys)) {
        ret = -ENOMEM;
        goto err_free_desc;
    }

    spin_lock_irqsave(&d->dma_lock, flags);

    /* Check if DMA engine is busy */
    status = readl(engine_base + DMA_REG_CONTROL_STATUS);
    if (status & DMA_STATUS_BUSY) {
        spin_unlock_irqrestore(&d->dma_lock, flags);
        dev_err(&d->pdev->dev, "DMA engine 0 busy (status=0x%08x)\n", status);
        ret = -EBUSY;
        goto err_unmap;
    }

    d->dma_active = true;
    reinit_completion(&d->dma_done);

    spin_unlock_irqrestore(&d->dma_lock, flags);

    /* Build descriptor */
    pciecntl_build_descriptor(d, card_addr, host_phys, length,
                              true, false, desc_virt);

    /* Write descriptor address to hardware */
    writel(lower_32_bits(desc_phys), engine_base + DMA_REG_DESCRIPTOR_LO);
    writel(upper_32_bits(desc_phys), engine_base + DMA_REG_DESCRIPTOR_HI);

    /* Ensure descriptor address is written before starting */
    wmb();

    /* Start DMA with IRQ enabled */
    writel(DMA_CTRL_START | DMA_CTRL_IRQ_EN,
           engine_base + DMA_REG_CONTROL_STATUS);

    dev_dbg(&d->pdev->dev, "DMA read started, desc_phys=0x%llx\n",
            (u64)desc_phys);

    if (sync) {
        /* Wait for completion with timeout */
        unsigned long timeout = msecs_to_jiffies(DMA_TIMEOUT_MS);

        ret = wait_for_completion_timeout(&d->dma_done, timeout);
        if (ret == 0) {
            dev_err(&d->pdev->dev, "DMA read timeout\n");

            /* Try to stop the engine */
            writel(DMA_CTRL_STOP, engine_base + DMA_REG_CONTROL_STATUS);

            ret = -ETIMEDOUT;
        } else {
            ret = 0;
        }

        /* Cleanup */
        dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_FROM_DEVICE);
        dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                          desc_virt, desc_phys);

        spin_lock_irqsave(&d->dma_lock, flags);
        d->dma_active = false;
        spin_unlock_irqrestore(&d->dma_lock, flags);
    }
    /* For async, caller must handle cleanup via completion callback */

    return ret;

    err_unmap:
    dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_FROM_DEVICE);
    err_free_desc:
    dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                      desc_virt, desc_phys);
    return ret;
}

/**
 * pciecntl_start_dma_write - Start DMA write from host to card
 * @d:          device context
 * @card_addr:  destination address in card RAM (byte address)
 * @host_buf:   source buffer (kernel virtual address)
 * @length:     transfer length in bytes
 * @sync:       if true, wait for completion before returning
 *
 * Returns 0 on success, negative error code on failure.
 */
int pciecntl_start_dma_write(struct c985_poc *d,
                             u32 card_addr,
                             const void *host_buf,
                             u32 length,
                             bool sync)
{
    struct ped_dma_descriptor *desc_virt;
    dma_addr_t desc_phys, host_phys;
    void __iomem *engine_base;
    u32 status;
    unsigned long flags;
    int ret = 0;

    if (!host_buf || length == 0)
        return -EINVAL;

    dev_dbg(&d->pdev->dev,
            "DMA write: card=0x%08x len=%u sync=%d\n",
            card_addr, length, sync);

    /* Use engine 1 for writes (S2C) */
    engine_base = pciecntl_engine_base(d, 1);

    /* Allocate descriptor */
    desc_virt = dma_alloc_coherent(&d->pdev->dev, sizeof(*desc_virt),
                                   &desc_phys, GFP_KERNEL);
    if (!desc_virt)
        return -ENOMEM;

    /* Map host buffer for DMA (card reads from host) */
    host_phys = dma_map_single(&d->pdev->dev, (void *)host_buf, length,
                               DMA_TO_DEVICE);
    if (dma_mapping_error(&d->pdev->dev, host_phys)) {
        ret = -ENOMEM;
        goto err_free_desc;
    }

    spin_lock_irqsave(&d->dma_lock, flags);

    /* Check if DMA engine is busy */
    status = readl(engine_base + DMA_REG_CONTROL_STATUS);
    if (status & DMA_STATUS_BUSY) {
        spin_unlock_irqrestore(&d->dma_lock, flags);
        dev_err(&d->pdev->dev, "DMA engine 1 busy (status=0x%08x)\n", status);
        ret = -EBUSY;
        goto err_unmap;
    }

    d->dma_active = true;
    reinit_completion(&d->dma_done);

    spin_unlock_irqrestore(&d->dma_lock, flags);

    /* Build descriptor */
    pciecntl_build_descriptor(d, card_addr, host_phys, length,
                              true, true, desc_virt);

    /* Write descriptor address to hardware */
    writel(lower_32_bits(desc_phys), engine_base + DMA_REG_DESCRIPTOR_LO);
    writel(upper_32_bits(desc_phys), engine_base + DMA_REG_DESCRIPTOR_HI);

    /* Ensure descriptor address is written before starting */
    wmb();

    /* Start DMA with IRQ enabled */
    writel(DMA_CTRL_START | DMA_CTRL_IRQ_EN,
           engine_base + DMA_REG_CONTROL_STATUS);

    dev_dbg(&d->pdev->dev, "DMA write started, desc_phys=0x%llx\n",
            (u64)desc_phys);

    if (sync) {
        /* Wait for completion with timeout */
        unsigned long timeout = msecs_to_jiffies(DMA_TIMEOUT_MS);

        ret = wait_for_completion_timeout(&d->dma_done, timeout);
        if (ret == 0) {
            dev_err(&d->pdev->dev, "DMA write timeout\n");

            /* Try to stop the engine */
            writel(DMA_CTRL_STOP, engine_base + DMA_REG_CONTROL_STATUS);

            ret = -ETIMEDOUT;
        } else {
            ret = 0;
        }

        /* Cleanup */
        dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_TO_DEVICE);
        dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                          desc_virt, desc_phys);

        spin_lock_irqsave(&d->dma_lock, flags);
        d->dma_active = false;
        spin_unlock_irqrestore(&d->dma_lock, flags);
    }

    return ret;

    err_unmap:
    dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_TO_DEVICE);
    err_free_desc:
    dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                      desc_virt, desc_phys);
    return ret;
}

/**
 * pciecntl_dma_read_done - DMA read completion handler
 * @d: device context
 *
 * Called from IRQ handler when DMA read (C2S) completes.
 */
void pciecntl_dma_read_done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "DMA read complete\n");
    complete(&d->dma_done);
}

/**
 * pciecntl_dma_write_done - DMA write completion handler
 * @d: device context
 *
 * Called from IRQ handler when DMA write (S2C) completes.
 */
void pciecntl_dma_write_done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "DMA write complete\n");
    complete(&d->dma_done);
}

/**
 * pciecntl_read_card_ram - Read from card RAM (synchronous)
 * @d:          device context
 * @card_addr:  source address in card RAM
 * @buf:        destination buffer
 * @length:     transfer length
 *
 * Convenience wrapper for synchronous DMA read.
 */
int pciecntl_read_card_ram(struct c985_poc *d,
                           u32 card_addr,
                           void *buf,
                           u32 length)
{
    return pciecntl_start_dma_read(d, card_addr, buf, length, true);
}

/**
 * pciecntl_write_card_ram - Write to card RAM (synchronous)
 * @d:          device context
 * @card_addr:  destination address in card RAM
 * @buf:        source buffer
 * @length:     transfer length
 *
 * Convenience wrapper for synchronous DMA write.
 */
int pciecntl_write_card_ram(struct c985_poc *d,
                            u32 card_addr,
                            const void *buf,
                            u32 length)
{
    return pciecntl_start_dma_write(d, card_addr, buf, length, true);
}

/**
 * CPCIeCntl_RegisterISR - Register interrupt service routine
 * @d: device context
 *
 * Returns 0 on success, negative error on failure.
 */
int CPCIeCntl_RegisterISR(struct c985_poc *d)
{
    int ret = 0;

    if (!d->irq_registered) {
        ret = request_irq(d->pdev->irq,
                          PciInterruptService,
                          IRQF_SHARED,
                          DRV_NAME,
                          d);

        if (ret == 0) {
            d->irq_registered = 1;
            dev_info(&d->pdev->dev, "IRQ %d registered\n", d->pdev->irq);
        } else {
            dev_err(&d->pdev->dev, "Failed to register IRQ %d: %d\n",
                    d->pdev->irq, ret);
        }
    }

    return ret;
}

/**
 * CPCIeCntl_UnregisterISR - Unregister interrupt service routine
 * @d: device context
 *
 * Returns 0 on success.
 */
int CPCIeCntl_UnregisterISR(struct c985_poc *d)
{
    if (d->irq_registered) {
        free_irq(d->pdev->irq, d);
        d->irq_registered = 0;
        dev_info(&d->pdev->dev, "IRQ %d unregistered\n", d->pdev->irq);
    }

    return 0;
}
u32 CPCIeCntl_GetMaxDMASize(struct c985_poc *param_1)
{
    u32 uVar2;
    u32 local_28;
    u32 local_10;
    size_t max_seg;

    local_28 = 0xFFFFFFF;

    if (param_1 != NULL && param_1->pdev != NULL) {
        /*
         * Windows uses DmaDevices[0].MapRegsAvailable (write channel)
         * and DmaDevices[1].MapRegsAvailable (read channel) to compute
         * max DMA size as (pages - 1) * 0x1000.
         *
         * Linux equivalent: query DMA segment size limits.
         */
        max_seg = dma_get_max_seg_size(&param_1->pdev->dev);
        if (max_seg == 0)
            max_seg = 0x10000;  /* 64KB default */

            /* Align down to page boundary, similar to Windows calc */
            local_28 = (max_seg / 0x1000) * 0x1000;

        dev_dbg(&param_1->pdev->dev,
                "CPCIeCntl_GetMaxDMASize() dwMaxDMASize(%u)\n", local_28);
    }

    return local_28;
}
