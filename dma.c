#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "structs.h"
#include "avermedia_c985.h"
#include "dma.h"
#include "ctask/ctask_private.h"

/* DMA descriptor layout (32 bytes) */
struct ped_dma_desc {
    u32 control;        /* [0] */
    u32 length;         /* [1] */
    u64 host_addr;      /* [2-3] */
    u64 card_addr;      /* [4-5] */
    u64 next_desc;      /* [6-7] */
};

/* DMA engine register offsets */
#define DMA_ENGINE_SIZE         0x100
#define DMA_REG_CAPS            0x00
#define DMA_REG_CTRL_STATUS     0x04
#define DMA_REG_DESC_LO         0x08
#define DMA_REG_DESC_HI         0x0C

/* Control status bits */
#define DMA_STATUS_BUSY         0x400
#define DMA_CTRL_START          0x101

/* Global DMA control */
#define DMA_GLOBAL_BASE         0x4000
#define DMA_GLOBAL_CTRL         0x00

/* Descriptor alignment */
#define DESC_ALIGN              32

/**
 * PedDmaComplete - Handle DMA completion for a channel
 */
void PedDmaComplete(struct c985_poc *d, u32 channel, u8 error)
{
    struct device_transfer *xfer;
    unsigned long flags;
    u32 event_type;

    dev_dbg(&d->pdev->dev, "PedDmaComplete() channel=%u error=%d\n", channel, error);

    if (channel >= d->pcie.m_NumDmaAvailable)
        return;

    xfer = &d->pcie.DmaDevices[channel];

    spin_lock_irqsave(&d->dma_lock, flags);
    xfer->hDmaRequest = NULL;
    xfer->UsedDescriptors = 0;
    xfer->bStarted = 0;
    spin_unlock_irqrestore(&d->dma_lock, flags);

    /* Callback: 2 = read complete, 4 = write complete */
    if (xfer->DmaDirection == DMA_DIR_READ) {
        event_type = 2;
    } else {
        event_type = 4;
    }

    if (d->pcie.pBusCallbackFunc) {
        ((int (*)(void *, u32, void *))d->pcie.pBusCallbackFunc)(
            d->pcie.pBusCallbackContext, event_type, NULL);
    }
}

/**
 * PedDmaDpcHandler - Process DMA interrupts (called from workqueue)
 */
void PedDmaDpcHandler(struct c985_poc *d)
{
    u32 i;

    dev_dbg(&d->pdev->dev, "PedDmaDpcHandler() InterruptStatus=0x%x\n",
            d->pcie.InterruptStatus);

    while (d->pcie.InterruptStatus != 0) {
        for (i = 0; i < d->pcie.m_NumDmaAvailable; i++) {
            if (d->pcie.InterruptStatus & (1 << i)) {
                d->pcie.InterruptStatus &= ~(1 << i);
                PedDmaComplete(d, i, 0);
            }
        }
        /* Clear unknown bits */
        if (d->pcie.InterruptStatus & 0xfffff0f0) {
            d->pcie.InterruptStatus &= 0x0f0f;
        }
    }
}

/**
 * PedDmaQueueBuffers - Build descriptors and start DMA
 */
long PedDmaQueueBuffers(struct c985_poc *d,
                        struct device_transfer *xfer,
                        dma_addr_t host_phys,
                        u32 card_addr,
                        size_t length,
                        bool is_write)
{
    void __iomem *engine_regs;
    struct ped_dma_desc *desc;
    dma_addr_t desc_phys_aligned;
    u64 card_addr_flags;
    u32 control;
    u32 status;
    unsigned long flags;

    engine_regs = c985_bar0(d) + (xfer->DmaIndex * DMA_ENGINE_SIZE);

    dev_dbg(&d->pdev->dev, "PedDmaQueueBuffers() %s len=%zu card=0x%x\n",
            is_write ? "Write" : "Read", length, card_addr);

    spin_lock_irqsave(&d->dma_lock, flags);

    /* Enable global DMA if needed */
    if ((readl(c985_bar0(d) + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL) & 1) == 0) {
        writel(1, c985_bar0(d) + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);
    }

    /* Check engine is idle */
    status = readl(engine_regs + DMA_REG_CTRL_STATUS);
    if (status & DMA_STATUS_BUSY) {
        dev_err(&d->pdev->dev,
                "PedDmaQueueBuffers() DMA%u is still busy. ControlStatus:0x%08X\n",
                xfer->DmaIndex, status);
        spin_unlock_irqrestore(&d->dma_lock, flags);
        return -EBUSY;
    }

    /* Get aligned descriptor address */
    desc_phys_aligned = (xfer->DmaDescriptor.PhysAddress + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1);
    desc = (struct ped_dma_desc *)(((unsigned long)xfer->DmaDescriptor.VirtAddress +
    DESC_ALIGN - 1) & ~(DESC_ALIGN - 1));

    /* Build transfer mode flags (mode 0 = default) */
    card_addr_flags = 0x400000000ULL;
    control = 0x0c02100f;

    /* Build data descriptor */
    desc[0].control = control;
    desc[0].length = length;
    desc[0].host_addr = host_phys;
    desc[0].card_addr = (u64)card_addr | card_addr_flags;

    if (is_write) {
        /* Chain to dummy descriptor */
        desc[0].next_desc = desc_phys_aligned + sizeof(struct ped_dma_desc);

        /* Build dummy descriptor for writes */
        desc[1].control = control;
        desc[1].length = 4;
        desc[1].host_addr = host_phys;
        desc[1].card_addr = 0x0800000000000000ULL;
        desc[1].next_desc = 0;

        xfer->UsedDescriptors = 2;
    } else {
        /* Read: set IRQ flag on card address */
        desc[0].card_addr |= 0x0800000000000000ULL;
        desc[0].next_desc = 0;

        xfer->UsedDescriptors = 1;
    }

    /* Memory barrier */
    wmb();

    dev_dbg(&d->pdev->dev,
            "PedDmaQueueBuffers() desc@0x%llx ctrl=0x%x len=%u host=0x%llx card=0x%llx\n",
            (u64)desc_phys_aligned, desc[0].control, desc[0].length,
            desc[0].host_addr, desc[0].card_addr);

    /* Write descriptor pointer to hardware */
    writel((u32)desc_phys_aligned, engine_regs + DMA_REG_DESC_LO);
    writel((u32)(desc_phys_aligned >> 32), engine_regs + DMA_REG_DESC_HI);

    /* Start DMA: 0x101 */
    writel(DMA_CTRL_START, engine_regs + DMA_REG_CTRL_STATUS);
    xfer->bStarted = 1;

    spin_unlock_irqrestore(&d->dma_lock, flags);

    /* Return pending status */
    return 0x103;
}

/**
 * CQLCodec_StartDMAWrite - Write data to card memory via DMA
 */
int CQLCodec_StartDMAWrite(struct c985_poc *d, const void *src,
                           u32 card_addr, size_t size)
{
    struct device_transfer *xfer;
    void *buf;
    dma_addr_t buf_phys;
    u32 evt_bits;
    long ret;

    if (size == 0 || src == NULL) {
        dev_dbg(&d->pdev->dev,
                "CQLCodec_StartDMAWrite() Param error ulLength(%zu), pHostAddr(%p)\n",
                size, src);
        return -EINVAL;
    }

    if (d->pcie.m_NumDmaAvailable == 0 || d->pcie.WriteDmaChannel < 0) {
        dev_err(&d->pdev->dev, "DMA not initialized\n");
        return -ENODEV;
    }

    xfer = &d->pcie.DmaDevices[d->pcie.WriteDmaChannel];

    dev_dbg(&d->pdev->dev,
            "CQLCodec_StartDMAWrite() Arm(0x%x) Host(%p) len(%zu)\n",
            card_addr, src, size);

    /* Allocate DMA buffer and copy data */
    buf = dma_alloc_coherent(&d->pdev->dev, size, &buf_phys, GFP_KERNEL);
    if (!buf) {
        dev_err(&d->pdev->dev, "Failed to allocate %zu byte DMA buffer\n", size);
        return -ENOMEM;
    }
    memcpy(buf, src, size);

    /* Clear sync event before starting */
    QPOSMClearEvtgrp(d->codec.m_EvtSyncDma, 2);

    /* Start DMA */
    ret = PedDmaQueueBuffers(d, xfer, buf_phys, card_addr, size, true);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "CQLCodec_StartDMAWrite() PedDmaQueueBuffers() Failed status(%ld)\n", ret);
        dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
        return ret;
    }

    /* Wait for completion */
    ret = QPOSMWaitEvtgrp(d->codec.m_EvtSyncDma, 2, &evt_bits, 2000);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "CQLCodec_StartDMAWrite() QPOSMWaitEvtgrp() Failed status(%ld)\n", ret);
        dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
        return ret;
    }

    dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
    return 0;
}

/**
 * CQLCodec_StartDMARead - Read data from card memory via DMA
 */
int CQLCodec_StartDMARead(struct c985_poc *d, void *dst,
                          u32 card_addr, size_t size)
{
    struct device_transfer *xfer;
    void *buf;
    dma_addr_t buf_phys;
    u32 evt_bits;
    long ret;

    if (size == 0 || dst == NULL) {
        dev_dbg(&d->pdev->dev,
                "CQLCodec_StartDMARead() Param error ulLength(%zu), pHostAddr(%p)\n",
                size, dst);
        return -EINVAL;
    }

    if (d->pcie.m_NumDmaAvailable == 0 || d->pcie.ReadDmaChannel < 0) {
        dev_err(&d->pdev->dev, "DMA not initialized\n");
        return -ENODEV;
    }

    xfer = &d->pcie.DmaDevices[d->pcie.ReadDmaChannel];

    dev_dbg(&d->pdev->dev,
            "CQLCodec_StartDMARead() Arm(0x%x) Host(%p) len(%zu)\n",
            card_addr, dst, size);

    /* Allocate DMA buffer */
    buf = dma_alloc_coherent(&d->pdev->dev, size, &buf_phys, GFP_KERNEL);
    if (!buf) {
        dev_err(&d->pdev->dev, "Failed to allocate %zu byte DMA buffer\n", size);
        return -ENOMEM;
    }

    /* Clear sync event before starting */
    QPOSMClearEvtgrp(d->codec.m_EvtSyncDma, 1);

    /* Start DMA */
    ret = PedDmaQueueBuffers(d, xfer, buf_phys, card_addr, size, false);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "CQLCodec_StartDMARead() PedDmaQueueBuffers() Failed status(%ld)\n", ret);
        dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
        return ret;
    }

    /* Wait for completion */
    ret = QPOSMWaitEvtgrp(d->codec.m_EvtSyncDma, 1, &evt_bits, 2000);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "CQLCodec_StartDMARead() QPOSMWaitEvtgrp() Failed status(%ld)\n", ret);
        dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
        return ret;
    }

    /* Copy data to destination */
    memcpy(dst, buf, size);

    dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);
    return 0;
}
/**
 * PedDmaInit - Initialize DMA subsystem
 */
int PedDmaInit(struct c985_poc *d)
{
    int i;
    u32 caps, max_transfer = 0;
    size_t ring_size;
    struct device_transfer *xfer;
    void *desc_virt;
    dma_addr_t desc_phys;

    dev_info(&d->pdev->dev, "PedDmaInit()\n");

    if (!c985_bar0(d)) {
        dev_err(&d->pdev->dev, "PedDmaInit: BAR0 not mapped!\n");
        return -EINVAL;
    }

    /* Initialize state */
    d->pcie.m_NumDmaAvailable = 0;
    d->pcie.InterruptEnable = 0;
    d->pcie.ReadDmaChannel = -1;
    d->pcie.WriteDmaChannel = -1;

    /* Check if interrupts available */
    if (!d->pcie.m_InterruptAvailable) {
        dev_warn(&d->pdev->dev, "PedDmaInit: No interrupts available\n");
        return 0;
    }

    /* Set DMA mask */
    if (dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(64)) == 0) {
        d->pcie.m_64BitAddress = 1;
        dev_info(&d->pdev->dev, "PedDmaInit: Using 64-bit DMA\n");
    } else if (dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(32)) == 0) {
        d->pcie.m_64BitAddress = 0;
        dev_info(&d->pdev->dev, "PedDmaInit: Using 32-bit DMA\n");
    } else {
        dev_err(&d->pdev->dev, "PedDmaInit: Failed to set DMA mask\n");
        return -EIO;
    }

    /* Scan all 64 possible DMA engines in BAR0 */
    for (i = 0; i < 64; i++) {
        u32 base = i * DMA_ENGINE_SIZE;
        u32 engine_max;
        bool is_c2s;

        caps = readl(c985_bar0(d) + base + DMA_REG_CAPS);

        /* Skip if not present */
        if (!(caps & 0x01))
            continue;
        if (caps == 0xFFFFFFFF)
            continue;

        /* Get pointer to this channel's transfer struct */
        xfer = &d->pcie.DmaDevices[d->pcie.m_NumDmaAvailable];
        memset(xfer, 0, sizeof(*xfer));

        /* Direction: bit 1 set = C2S (read from card) */
        is_c2s = (caps & 0x02) != 0;

        /* Max transfer size from caps bits 20:16 */
        engine_max = 1 << ((caps >> 16) & 0x1F);
        if (engine_max > max_transfer)
            max_transfer = engine_max;

        /* Fill transfer struct */
        xfer->DmaDirection = is_c2s ? DMA_DIR_READ : DMA_DIR_WRITE;
        xfer->DmaChannel = d->pcie.m_NumDmaAvailable;
        xfer->DmaIndex = i;
        xfer->bStarted = 0;
        xfer->NumDescriptors = 0x1002;

        /* Allocate descriptor ring */
        ring_size = (xfer->NumDescriptors * 0x20) + 0x20;
        desc_virt = dma_alloc_coherent(&d->pdev->dev, ring_size,
                                       &desc_phys, GFP_KERNEL);
        if (!desc_virt) {
            dev_err(&d->pdev->dev,
                    "PedDmaInit: Failed to alloc descriptors for DMA[%d]\n", i);
            continue;
        }
        memset(desc_virt, 0, ring_size);

        xfer->DmaDescriptor.VirtAddress = desc_virt;
        xfer->DmaDescriptor.PhysAddress = desc_phys;

        /* Enable this engine */
        writel(0x01, c985_bar0(d) + base + DMA_REG_CTRL_STATUS);

        /* Track first read/write channel */
        if (is_c2s) {
            if (d->pcie.ReadDmaChannel == -1)
                d->pcie.ReadDmaChannel = d->pcie.m_NumDmaAvailable;
        } else {
            if (d->pcie.WriteDmaChannel == -1)
                d->pcie.WriteDmaChannel = d->pcie.m_NumDmaAvailable;
        }

        dev_info(&d->pdev->dev,
                 "PedDmaInit: DMA[%2d] %s ch=%d caps=0x%08x desc=%p[0x%llx]\n",
                 i,
                 is_c2s ? "C2S(read) " : "S2C(write)",
                 d->pcie.m_NumDmaAvailable,
                 caps,
                 desc_virt,
                 (unsigned long long)desc_phys);

        d->pcie.m_NumDmaAvailable++;
    }

    /* Must have at least one channel */
    if (d->pcie.m_NumDmaAvailable == 0) {
        dev_err(&d->pdev->dev, "PedDmaInit: No DMA engines found\n");
        return -ENODEV;
    }

    d->pcie.m_DmaMaxTransfer = max_transfer;

    /* Enable global DMA */
    writel(0x01, c985_bar0(d) + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);

    dev_info(&d->pdev->dev,
             "PedDmaInit: %d channels, MaxXfer=0x%x, Read=%d Write=%d\n",
             d->pcie.m_NumDmaAvailable,
             max_transfer,
             d->pcie.ReadDmaChannel,
             d->pcie.WriteDmaChannel);

    return 0;
}
