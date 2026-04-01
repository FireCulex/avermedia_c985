// SPDX-License-Identifier: GPL-2.0
/*
 * pciecntl.c - PCIe DMA controller for AVerMedia C985
 *
 * DMA engine uses descriptor-based scatter-gather transfers.
 * Each descriptor is 32 bytes and forms a linked list.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "avermedia_c985.h"
#include "pciecntl.h"

/*
 * DMA engine register offsets (from HCI base at BAR1 + 0x810)
 * Each engine has its own register block.
 */
#define DMA_ENGINE_SIZE         0x20
#define DMA_ENGINE_0_BASE       0x0810

#define DMA_REG_CONTROL_STATUS  0x00
#define DMA_REG_CAPABILITIES    0x04
#define DMA_REG_DESCRIPTOR_LO   0x08
#define DMA_REG_DESCRIPTOR_HI   0x0C
// ... other registers at 0x10, 0x14, 0x18, 0x1C

/*
 * DMA control/status bits
 */
#define DMA_CTRL_START          BIT(0)  /* Start DMA */
#define DMA_CTRL_IRQ_EN         BIT(8)  /* Enable completion IRQ */
#define DMA_STATUS_BUSY         BIT(10) /* DMA engine busy */

/*
 * DMA descriptor control word bits
 */
#define DESC_CTRL_ENABLE        0x0000000F  /* Descriptor valid */
#define DESC_CTRL_MODE_3        0x0804200F  /* Mode 3: compressed video */

/*
 * Card address extended flags
 */
#define CARD_ADDR_MODE_FLAG     BIT_ULL(34) /* Mode enable bit */
#define CARD_ADDR_LAST_DESC     BIT_ULL(59) /* Last descriptor in chain */

/**
 * struct dma_descriptor - Hardware DMA descriptor (32 bytes)
 *
 * Must be aligned to 32-byte boundary (0x20).
 */
struct dma_descriptor {
    u32 control;            /* Control flags */
    u32 length;             /* Transfer length in bytes */
    u64 host_phys_addr;     /* Host physical address */
    u64 card_addr_ex;       /* Card address + extended flags */
    u64 next_descriptor;    /* Physical address of next descriptor (0 = last) */
} __packed __aligned(32);

/**
 * struct dma_transfer_ctx - DMA transfer context
 */
struct dma_transfer_ctx {
    struct c985_poc *d;
    struct dma_descriptor *desc_virt;
    dma_addr_t desc_phys;
    size_t desc_count;
    void *host_buf;
    dma_addr_t host_phys;
    size_t length;
    void (*callback)(int status, void *data);
    void *callback_data;
};

/**
 * pciecntl_init - Initialize PCIe DMA controller
 */
int pciecntl_init(struct c985_poc *d)
{
    int ret;

    /* Enable DMA on this PCI device */
    ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        /* Try 32-bit */
        ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(32));
        if (ret) {
            dev_err(&d->pdev->dev, "Failed to set DMA mask\n");
            return ret;
        }
    }

    spin_lock_init(&d->dma_lock);
    init_completion(&d->dma_done);
    d->dma_active = false;

    dev_info(&d->pdev->dev, "DMA: initialized, 64-bit capable\n");
    return 0;
}

/**
 * pciecntl_cleanup - Cleanup DMA controller
 */
void pciecntl_cleanup(struct c985_poc *d)
{
    /* Cancel any pending DMA */
    if (d->dma_active) {
        dev_warn(&d->pdev->dev, "DMA still active during cleanup\n");
    }
}


/**
 * pciecntl_build_descriptor_chain - Build DMA descriptor chain
 * @d:          device context
 * @card_addr:  source address in card RAM (byte address)
 * @host_phys:  destination physical address
 * @length:     transfer length
 * @is_last:    true if this is the last descriptor
 * @desc_out:   output descriptor structure
 *
 * Builds one descriptor for the chain. For now we do single-descriptor
 * transfers; scatter-gather can be added later.
 */
static void pciecntl_build_descriptor(struct c985_poc *d,
                                      u64 card_addr,
                                      u64 host_phys,
                                      u32 length,
                                      bool is_last,
                                      struct dma_descriptor *desc)
{
    u64 card_addr_ex;

    memset(desc, 0, sizeof(*desc));

    /*
     * Control word for mode 3 (compressed video):
     *   - 0x0804200F = descriptor enabled, mode 3
     */
    desc->control = DESC_CTRL_MODE_3;

    /* Transfer length */
    desc->length = length;

    /* Host physical address */
    desc->host_phys_addr = host_phys;

    /*
     * Card address extended:
     *   - Lower 32 bits = card byte address
     *   - Bit 34 = mode flag
     *   - Bit 59 = last descriptor (for reads)
     */
    card_addr_ex = card_addr | CARD_ADDR_MODE_FLAG;

    if (is_last)
        card_addr_ex |= CARD_ADDR_LAST_DESC;

    desc->card_addr_ex = card_addr_ex;

    /* Next descriptor (0 for last) */
    desc->next_descriptor = 0;

    dev_dbg(&d->pdev->dev,
            "DESC: ctrl=0x%08x len=%u host=0x%llx card=0x%llx next=0x%llx\n",
            desc->control, desc->length,
            desc->host_phys_addr, desc->card_addr_ex,
            desc->next_descriptor);
}

/**
 * pciecntl_start_dma_read - Start DMA read from card to host
 * @d:          device context
 * @card_addr:  source address in card RAM (byte address)
 * @host_buf:   destination buffer
 * @length:     transfer length in bytes
 * @sync:       if true, wait for completion
 */
// CPCIeCntl_StartDMARead
int pciecntl_start_dma_read(struct c985_poc *d,
                            u32 card_addr,
                            void *host_buf,
                            u32 length,
                            bool sync)
{
    struct dma_descriptor *desc_virt;
    dma_addr_t desc_phys, host_phys;
    u32 status;
    unsigned long flags;
    int ret = 0;

    if (!host_buf || length == 0)
        return -EINVAL;

    dev_dbg(&d->pdev->dev,
            "DMA read: card=0x%08x len=%u sync=%d\n",
            card_addr, length, sync);

    /* Allocate descriptor (must be 32-byte aligned) */
    desc_virt = dma_alloc_coherent(&d->pdev->dev, sizeof(*desc_virt),
                                   &desc_phys, GFP_KERNEL);
    if (!desc_virt)
        return -ENOMEM;

    /* Map host buffer for DMA */
    host_phys = dma_map_single(&d->pdev->dev, host_buf, length,
                               DMA_FROM_DEVICE);
    if (dma_mapping_error(&d->pdev->dev, host_phys)) {
        ret = -ENOMEM;
        goto err_free_desc;
    }

    spin_lock_irqsave(&d->dma_lock, flags);

    /* Check if DMA engine is busy */
    status = readl(d->bar1 + DMA_ENGINE_0_BASE + DMA_REG_CONTROL_STATUS);
    if (status & DMA_STATUS_BUSY) {
        spin_unlock_irqrestore(&d->dma_lock, flags);
        dev_err(&d->pdev->dev, "DMA engine busy (status=0x%08x)\n", status);
        ret = -EBUSY;
        goto err_unmap;
    }

    d->dma_active = true;
    reinit_completion(&d->dma_done);

    spin_unlock_irqrestore(&d->dma_lock, flags);

    /* Build descriptor */
    pciecntl_build_descriptor(d, card_addr, host_phys, length, true, desc_virt);

    /* Write descriptor address to hardware */
    writel(lower_32_bits(desc_phys),
           d->bar1 + DMA_ENGINE_0_BASE + DMA_REG_DESCRIPTOR_LO);
    writel(upper_32_bits(desc_phys),
           d->bar1 + DMA_ENGINE_0_BASE + DMA_REG_DESCRIPTOR_HI);

    /* Memory barrier */
    wmb();

    /* Start DMA: set bit 0 (start) and bit 8 (IRQ enable) */
    writel(DMA_CTRL_START | DMA_CTRL_IRQ_EN,
           d->bar1 + DMA_ENGINE_0_BASE + DMA_REG_CONTROL_STATUS);

    dev_dbg(&d->pdev->dev, "DMA started, desc_phys=0x%llx\n",
            (u64)desc_phys);

    if (sync) {
        /* Wait for completion */
        unsigned long timeout = msecs_to_jiffies(2000);

        ret = wait_for_completion_timeout(&d->dma_done, timeout);
        if (ret == 0) {
            dev_err(&d->pdev->dev, "DMA timeout\n");
            ret = -ETIMEDOUT;
        } else {
            ret = 0;
        }

        /* Unmap buffer */
        dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_FROM_DEVICE);

        /* Free descriptor */
        dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                          desc_virt, desc_phys);

        spin_lock_irqsave(&d->dma_lock, flags);
        d->dma_active = false;
        spin_unlock_irqrestore(&d->dma_lock, flags);
    }

    return ret;

    err_unmap:
    dma_unmap_single(&d->pdev->dev, host_phys, length, DMA_FROM_DEVICE);
    err_free_desc:
    dma_free_coherent(&d->pdev->dev, sizeof(*desc_virt),
                      desc_virt, desc_phys);
    return ret;
}

/**
 * pciecntl_dma_read_done - DMA read completion handler
 * @d: device context
 *
 * Called from IRQ handler when DMA completes.
 */
void pciecntl_dma_read_done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "DMA read complete\n");
    complete(&d->dma_done);
}

void pciecntl_dma_write_done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "DMA write complete\n");
    complete(&d->dma_done);
}

int pciecntl_read_card_ram(struct c985_poc *d,
                           u32 card_addr,
                           void *buf,
                           u32 length)
{
    /* For now, always use DMA */
    return pciecntl_start_dma_read(d, card_addr, buf, length, true);
}
