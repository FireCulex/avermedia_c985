// SPDX-License-Identifier: GPL-2.0
/*
 * dma.c - DMA engine for AVerMedia C985
 *
 * Based on PED_DMA_ENGINE structure from decompiled Windows driver.
 *
 * DMA engine layout in BAR0:
 *   0x0000 - 0x3FFF: PED_DMA_ENGINE[64] - 64 engines, 0x100 bytes each
 *   0x4000 - 0x4007: PED_DMA_COMMON - global control
 */

#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "avermedia_c985.h"
#include "dma.h"
#include "cpr.h"
#include "interrupts.h"

/* ========== Hardware Constants ========== */

/* Each DMA engine is 0x100 bytes (64 engines in 0x4000 = 0x100 each) */
#define DMA_ENGINE_SIZE         0x100
#define DMA_ENGINE_COUNT        64

/* PED_DMA_ENGINE register offsets */
#define DMA_REG_CAPS            0x00    /* R: Capabilities */
#define DMA_REG_CTRL_STATUS     0x04    /* R/W: Control & Status */
#define DMA_REG_DESC_LO         0x08    /* R/W: Descriptor pointer low */
#define DMA_REG_DESC_HI         0x0C    /* R/W: Descriptor pointer high */
#define DMA_REG_HW_TIME         0x10    /* R: Hardware time */
#define DMA_REG_CHAIN_CMPL      0x14    /* R: Chain complete byte count */

/* PED_DMA_COMMON at 0x4000 */
#define DMA_GLOBAL_BASE         0x4000
#define DMA_GLOBAL_CTRL         0x00    /* Control/Status */
#define DMA_GLOBAL_VERSION      0x04    /* FPGA Version */

/* Capabilities register bits */
#define DMA_CAP_PRESENT         0x01    /* Engine present */
#define DMA_CAP_C2S             0x02    /* Card-to-System (read), else S2C (write) */
#define DMA_CAP_IRQ_SHIFT       16      /* IRQ line number in bits 16+ */

/* Control/Status register bits */
#define DMA_CTRL_START          0x0101  /* Start transfer */
#define DMA_CTRL_STOP           0x0000  /* Stop/reset */
#define DMA_STATUS_BUSY         0x0400  /* Engine busy (bit 10) */
#define DMA_STATUS_DONE         0x0800  /* Transfer complete */

/* Descriptor control word (from Windows driver) */
#define DESC_CTRL_DEFAULT       0x0C02100F  /* Default for linear transfer */
#define DESC_CARD_ADDR_IRQ      0x0800000000000000ULL  /* IRQ on completion */

/* Timeouts */
#define DMA_TIMEOUT_MS          5000
#define DMA_POLL_INTERVAL_US    100

#define MAX_DMA_CHANNELS    8

/* ========== Hardware Descriptor ========== */

/**
 * struct c985_hw_desc - 32-byte hardware DMA descriptor
 *
 * Must be 32-byte aligned per Windows driver:
 *   (PhysAddress + 0x1F) & ~0x1F
 */
struct c985_hw_desc {
    u32 control;        /* 0x00: Control word */
    u32 length;         /* 0x04: Transfer length in bytes */
    u64 host_addr;      /* 0x08: Host physical address */
    u64 card_addr;      /* 0x10: Card address + flags in upper bits */
    u64 next_desc;      /* 0x18: Next descriptor physical address, 0=end */
} __packed;

#define DESC_ALIGN      32
#define MAX_CHUNK_SIZE  (64 * 1024)  /* 64KB per descriptor */

/* ========== Driver State ========== */

/* Discovered DMA engine info */
struct dma_engine_info {
    int index;          /* Engine index 0-63 */
    u32 base;           /* Register base offset */
    u32 caps;           /* Capabilities register */
    bool is_c2s;        /* true=read (C2S), false=write (S2C) */
    int irq_line;       /* IRQ line number */
};

static struct dma_engine_info s2c_engine;  /* Write engine */
static struct dma_engine_info c2s_engine;  /* Read engine */
static bool engines_found = false;

/* ========== Debug Helpers ========== */

static void dump_descriptor(struct c985_poc *d, int idx, struct c985_hw_desc *desc)
{
    dev_dbg(&d->pdev->dev,
             "  Desc[%d]: ctrl=0x%08x len=0x%x host=0x%llx card=0x%llx next=0x%llx\n",
             idx, desc->control, desc->length,
             desc->host_addr, desc->card_addr, desc->next_desc);
}

static void dump_engine_regs(struct c985_poc *d, const char *name, u32 base)
{
    u32 caps = readl(d->bar0 + base + DMA_REG_CAPS);
    u32 ctrl = readl(d->bar0 + base + DMA_REG_CTRL_STATUS);
    u32 desc_lo = readl(d->bar0 + base + DMA_REG_DESC_LO);
    u32 desc_hi = readl(d->bar0 + base + DMA_REG_DESC_HI);
    u32 hw_time = readl(d->bar0 + base + DMA_REG_HW_TIME);
    u32 chain = readl(d->bar0 + base + DMA_REG_CHAIN_CMPL);

    dev_dbg(&d->pdev->dev,
             "%s @ 0x%04x: caps=0x%08x ctrl=0x%08x desc=0x%08x%08x hwtime=0x%x chain=%u\n",
             name, base, caps, ctrl, desc_hi, desc_lo, hw_time, chain);
}

/* ========== Engine Discovery ========== */

/**
 * scan_dma_engines - Find all DMA engines and their capabilities
 / ***
 * scan_dma_engines - Find all DMA engines and their capabilities
 */
static void scan_dma_engines(struct c985_poc *d)
{
    int i;
    int s2c_count = 0, c2s_count = 0;

    dev_dbg(&d->pdev->dev, "=== SCANNING DMA ENGINES ===\n");

    /* Read global control first */
    {
        u32 global_ctrl = readl(d->bar0 + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);
        u32 global_ver = readl(d->bar0 + DMA_GLOBAL_BASE + DMA_GLOBAL_VERSION);
        dev_info(&d->pdev->dev, "Global: ctrl=0x%08x version=0x%08x\n",
                 global_ctrl, global_ver);
    }

    /* Reset channel count */
    d->num_dma_channels = 0;

    /* Scan all 64 possible engines */
    for (i = 0; i < DMA_ENGINE_COUNT; i++) {
        u32 base = i * DMA_ENGINE_SIZE;
        u32 caps = readl(d->bar0 + base + DMA_REG_CAPS);
        u32 ctrl = readl(d->bar0 + base + DMA_REG_CTRL_STATUS);
        bool is_c2s;
        int irq;

        /* Skip if no engine present */
        if (!(caps & DMA_CAP_PRESENT))
            continue;
        if (caps == 0xFFFFFFFF)
            continue;

        is_c2s = (caps & DMA_CAP_C2S) != 0;
        irq = (caps >> DMA_CAP_IRQ_SHIFT) & 0xFF;

        dev_dbg(&d->pdev->dev,
                 "DMA[%2d] @ 0x%04x: caps=0x%08x ctrl=0x%08x %s IRQ=%d\n",
                 i, base, caps, ctrl, is_c2s ? "C2S(read)" : "S2C(write)", irq);

        /* Store engine index for interrupt handler */
        if (d->num_dma_channels < MAX_DMA_CHANNELS) {
            d->dma_engine_idx[d->num_dma_channels] = i;
            d->num_dma_channels++;
        }

        /* Remember first engine of each type for transfers */
        if (is_c2s) {
            if (c2s_count == 0) {
                c2s_engine.index = i;
                c2s_engine.base = base;
                c2s_engine.caps = caps;
                c2s_engine.is_c2s = true;
                c2s_engine.irq_line = irq;
            }
            c2s_count++;
        } else {
            if (s2c_count == 0) {
                s2c_engine.index = i;
                s2c_engine.base = base;
                s2c_engine.caps = caps;
                s2c_engine.is_c2s = false;
                s2c_engine.irq_line = irq;
            }
            s2c_count++;
        }
    }

    dev_dbg(&d->pdev->dev, "Found %d S2C (write) and %d C2S (read) engines\n",
             s2c_count, c2s_count);

    if (s2c_count > 0 && c2s_count > 0) {
        engines_found = true;
        dev_dbg(&d->pdev->dev, "Using: Write=DMA[%d]@0x%x, Read=DMA[%d]@0x%x\n",
                 s2c_engine.index, s2c_engine.base,
                 c2s_engine.index, c2s_engine.base);
    } else {
        dev_err(&d->pdev->dev, "ERROR: Missing required DMA engines!\n");
    }
}
/* ========== DMA Transfer Core ========== */

/**
 * wait_engine_idle - Wait for DMA engine to complete
 */
static int wait_engine_idle(struct c985_poc *d, u32 base, const char *name)
{
    unsigned long timeout = jiffies + msecs_to_jiffies(DMA_TIMEOUT_MS);
    u32 status;
    int polls = 0;

    while (time_before(jiffies, timeout)) {
        status = readl(d->bar0 + base + DMA_REG_CTRL_STATUS);
        polls++;

        if (!(status & DMA_STATUS_BUSY)) {
            u32 chain = readl(d->bar0 + base + DMA_REG_CHAIN_CMPL);
            dev_dbg(&d->pdev->dev,
                     "%s complete: status=0x%08x polls=%d bytes=%u\n",
                     name, status, polls, chain);
            return 0;
        }

        usleep_range(DMA_POLL_INTERVAL_US, DMA_POLL_INTERVAL_US * 2);
    }

    status = readl(d->bar0 + base + DMA_REG_CTRL_STATUS);
    dev_err(&d->pdev->dev, "%s TIMEOUT: status=0x%08x after %d polls\n",
            name, status, polls);

    /* Dump engine state on timeout */
    dump_engine_regs(d, name, base);

    return -ETIMEDOUT;
}

/**
 * do_dma_transfer - Execute a DMA transfer
 */
/**
 * do_dma_transfer - Execute a DMA transfer
 *
 * Card address upper bits encode transfer parameters:
 *   Bits 63-56: DataSwap
 *   Bit 55:     IRQ on completion (for reads)
 *   Bit 34:     Transfer mode flag
 *   Bits 31-0:  Card memory address
 */
static int do_dma_transfer(struct c985_poc *d, struct dma_engine_info *engine,
                           dma_addr_t host_phys, u32 card_addr, size_t size,
                           bool is_write)
{
    struct c985_hw_desc *descs = NULL;
    dma_addr_t descs_phys;
    dma_addr_t descs_aligned;
    struct c985_hw_desc *descs_base;
    int num_descs;
    int i;
    int ret;
    u32 status;
    u64 card_addr_flags;

    const char *name = is_write ? "Write" : "Read";

    dev_dbg(&d->pdev->dev, "=== DMA %s: %zu bytes, card=0x%08x ===\n",
             name, size, card_addr);

    /* Check engine is idle */
    status = readl(d->bar0 + engine->base + DMA_REG_CTRL_STATUS);
    if (status & DMA_STATUS_BUSY) {
        dev_err(&d->pdev->dev, "%s engine busy: 0x%08x\n", name, status);
        return -EBUSY;
    }

    /* Calculate number of descriptors needed */
    num_descs = (size + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;

    /* For writes, add one dummy descriptor (from Windows driver) */
    int total_descs = is_write ? num_descs + 1 : num_descs;

    dev_dbg(&d->pdev->dev, "Need %d descriptors (%d data + %d dummy)\n",
             total_descs, num_descs, is_write ? 1 : 0);

    /* Allocate descriptor buffer (extra space for alignment) */
    descs = dma_alloc_coherent(&d->pdev->dev,
                               total_descs * sizeof(*descs) + DESC_ALIGN,
                               &descs_phys, GFP_KERNEL);
    if (!descs) {
        dev_err(&d->pdev->dev, "Failed to allocate descriptors\n");
        return -ENOMEM;
    }

    /* Save base for freeing later */
    descs_base = descs;

    /* Align to 32 bytes */
    descs_aligned = (descs_phys + DESC_ALIGN - 1) & ~(DESC_ALIGN - 1);
    descs = (struct c985_hw_desc *)((u8 *)descs + (descs_aligned - descs_phys));

    memset(descs, 0, total_descs * sizeof(*descs));

    dev_dbg(&d->pdev->dev, "Descriptors: phys=0x%llx aligned=0x%llx\n",
             (u64)descs_phys, (u64)descs_aligned);

    /*
     * Build card address with mode flags.
     * From Windows driver (transfer mode 0 / default):
     *   card_addr |= 0x400000000  (bit 34)
     *   card_addr |= DataSwap << 56
     * DataSwap = 0 for default
     */
    card_addr_flags = 0x400000000ULL;  /* Transfer mode bit */

    /* Build data descriptor chain */
    for (i = 0; i < num_descs; i++) {
        size_t offset = i * MAX_CHUNK_SIZE;
        size_t chunk = min(size - offset, (size_t)MAX_CHUNK_SIZE);

        descs[i].control = DESC_CTRL_DEFAULT;
        descs[i].length = chunk;
        descs[i].host_addr = host_phys + offset;
        descs[i].card_addr = (u64)(card_addr + offset) | card_addr_flags;

        /* Last data descriptor */
        if (i == num_descs - 1) {
            if (!is_write) {
                /* Read: set IRQ flag, null next pointer = end */
                descs[i].card_addr |= DESC_CARD_ADDR_IRQ;
                descs[i].next_desc = 0;
            } else {
                /* Write: chain to dummy descriptor */
                descs[i].next_desc = descs_aligned + (i + 1) * sizeof(*descs);
            }
        } else {
            descs[i].next_desc = descs_aligned + (i + 1) * sizeof(*descs);
        }

        dump_descriptor(d, i, &descs[i]);
    }

    /*
     * For writes: add dummy descriptor (from PedDmaQueueBuffers)
     * The Windows driver appends a 4-byte dummy write after the real data.
     * card_addr upper bits: 0x0800000000000000 (IRQ bit in different position)
     * next_desc = 0 (end of chain)
     */
    if (is_write) {
        int dummy_idx = num_descs;
        descs[dummy_idx].control = DESC_CTRL_DEFAULT;
        descs[dummy_idx].length = 4;
        /* Reuse last host address */
        descs[dummy_idx].host_addr = descs[num_descs - 1].host_addr;
        /* Card addr word 1 = 0, word 2 = 0x08000000 */
        descs[dummy_idx].card_addr = 0x0800000000000000ULL;
        descs[dummy_idx].next_desc = 0;

        dev_dbg(&d->pdev->dev, "  Dummy[%d]: ctrl=0x%08x len=4 card=0x%llx\n",
                 dummy_idx, descs[dummy_idx].control, descs[dummy_idx].card_addr);
    }

    /* Memory barrier before hardware sees descriptors */
    wmb();

    /* Dump engine state before */
    dump_engine_regs(d, name, engine->base);

    /* Write descriptor pointer (split 32-bit writes for reliability) */
    writel((u32)descs_aligned, d->bar0 + engine->base + DMA_REG_DESC_LO);
    writel((u32)(descs_aligned >> 32), d->bar0 + engine->base + DMA_REG_DESC_HI);

    /* Verify write */
    {
        u32 lo = readl(d->bar0 + engine->base + DMA_REG_DESC_LO);
        u32 hi = readl(d->bar0 + engine->base + DMA_REG_DESC_HI);
        dev_dbg(&d->pdev->dev, "Descriptor ptr set: 0x%08x%08x\n", hi, lo);
    }

    /* Start transfer */
    dev_dbg(&d->pdev->dev, "Starting %s DMA...\n", name);
    writel(DMA_CTRL_START, d->bar0 + engine->base + DMA_REG_CTRL_STATUS);

    /* Immediately check status */
    status = readl(d->bar0 + engine->base + DMA_REG_CTRL_STATUS);
    dev_dbg(&d->pdev->dev, "Status after start: 0x%08x\n", status);

    /* Wait for completion */
    ret = wait_engine_idle(d, engine->base, name);

    /* Dump engine state after */
    dump_engine_regs(d, name, engine->base);

    /* Free descriptors */
    dma_free_coherent(&d->pdev->dev,
                      total_descs * sizeof(*descs) + DESC_ALIGN,
                      descs_base, descs_phys);

    return ret;
}

/* ========== Public API ========== */

/**
 * c985_dma_init - Initialize DMA subsystem
 */
int c985_dma_init(struct c985_poc *d)
{
    u32 global_ctrl;
    int ret;

    dev_info(&d->pdev->dev, "=== DMA INIT ===\n");

    if (!d->bar0) {
        dev_err(&d->pdev->dev, "BAR0 not mapped!\n");
        return -EINVAL;
    }

    /* Set DMA mask */
    ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(64));
    if (ret) {
        ret = dma_set_mask_and_coherent(&d->pdev->dev, DMA_BIT_MASK(32));
        if (ret) {
            dev_err(&d->pdev->dev, "Failed to set DMA mask\n");
            return ret;
        }
        dev_info(&d->pdev->dev, "Using 32-bit DMA\n");
    } else {
        dev_info(&d->pdev->dev, "Using 64-bit DMA\n");
    }

    /* Scan for available engines */
    scan_dma_engines(d);

    if (!engines_found) {
        dev_err(&d->pdev->dev, "No usable DMA engines found\n");
        return -ENODEV;
    }


    /* Enable global DMA if needed */
    global_ctrl = readl(d->bar0 + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);
    if (!(global_ctrl & 1)) {
        dev_info(&d->pdev->dev, "Enabling global DMA control\n");
        writel(global_ctrl | 1, d->bar0 + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);
        global_ctrl = readl(d->bar0 + DMA_GLOBAL_BASE + DMA_GLOBAL_CTRL);
        dev_info(&d->pdev->dev, "Global control now: 0x%08x\n", global_ctrl);
    }

    dev_info(&d->pdev->dev, "DMA initialized successfully\n");
    return 0;
}

/**
 * c985_dma_cleanup - Cleanup DMA subsystem
 */
void c985_dma_cleanup(struct c985_poc *d)
{
    dev_info(&d->pdev->dev, "DMA cleanup\n");
    engines_found = false;
}

/**
 * c985_dma_write_sync - Write data to card memory via DMA
 */
int c985_dma_write_sync(struct c985_poc *d, const void *src,
                        u32 card_addr, size_t size)
{
    void *buf;
    dma_addr_t buf_phys;
    int ret;

    if (!engines_found) {
        dev_err(&d->pdev->dev, "DMA not initialized\n");
        return -ENODEV;
    }

    if (size == 0)
        return 0;

    /* Allocate DMA buffer and copy data */
    buf = dma_alloc_coherent(&d->pdev->dev, size, &buf_phys, GFP_KERNEL);
    if (!buf) {
        dev_err(&d->pdev->dev, "Failed to allocate %zu byte DMA buffer\n", size);
        return -ENOMEM;
    }

    memcpy(buf, src, size);

    dev_dbg(&d->pdev->dev, "Write buffer: virt=%p phys=0x%llx size=%zu\n",
             buf, (u64)buf_phys, size);

    /* Do the transfer */
    ret = do_dma_transfer(d, &s2c_engine, buf_phys, card_addr, size, true);

    dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);

    return ret;
}

/**
 * c985_dma_read_sync - Read data from card memory via DMA
 */
int c985_dma_read_sync(struct c985_poc *d, void *dst,
                       u32 card_addr, size_t size)
{
    void *buf;
    dma_addr_t buf_phys;
    int ret;

    if (!engines_found) {
        dev_err(&d->pdev->dev, "DMA not initialized\n");
        return -ENODEV;
    }

    if (size == 0)
        return 0;

    /* Allocate DMA buffer */
    buf = dma_alloc_coherent(&d->pdev->dev, size, &buf_phys, GFP_KERNEL);
    if (!buf) {
        dev_err(&d->pdev->dev, "Failed to allocate %zu byte DMA buffer\n", size);
        return -ENOMEM;
    }

    memset(buf, 0xAA, size);  /* Fill with pattern to detect reads */

    dev_dbg(&d->pdev->dev, "Read buffer: virt=%p phys=0x%llx size=%zu\n",
             buf, (u64)buf_phys, size);

    /* Do the transfer */
    ret = do_dma_transfer(d, &c2s_engine, buf_phys, card_addr, size, false);

    if (ret == 0) {
        memcpy(dst, buf, size);

        /* Dump first 64 bytes */
        dev_dbg(&d->pdev->dev, "Read data (first 64 bytes):\n");
        print_hex_dump(KERN_INFO, "  ", DUMP_PREFIX_OFFSET, 16, 4,
                       buf, min(size, (size_t)64), true);
    }

    dma_free_coherent(&d->pdev->dev, size, buf, buf_phys);

    return ret;
}

/* ========== Test Functions ========== */

/**
 * c985_dma_test_loopback - Test DMA write then read back via DMA
 */
int c985_dma_test_loopback(struct c985_poc *d)
{
    u8 write_buf[256];
    u8 read_buf[256];
    u32 test_addr = 0x10000;  /* Use address unlikely to conflict */
    int i;
    int ret;
    int mismatches = 0;

    dev_info(&d->pdev->dev, "=== DMA LOOPBACK TEST ===\n");

    /* Create test pattern */
    for (i = 0; i < 256; i++)
        write_buf[i] = i ^ 0xAA;

    /* Write via DMA */
    ret = c985_dma_write_sync(d, write_buf, test_addr, 256);
    if (ret) {
        dev_err(&d->pdev->dev, "Loopback: write failed %d\n", ret);
        return ret;
    }

    /* Small delay */
    usleep_range(1000, 2000);

    /* Read via DMA */
    memset(read_buf, 0, sizeof(read_buf));
    ret = c985_dma_read_sync(d, read_buf, test_addr, 256);
    if (ret) {
        dev_err(&d->pdev->dev, "Loopback: read failed %d\n", ret);
        return ret;
    }

    /* Compare */
    for (i = 0; i < 256; i++) {
        if (write_buf[i] != read_buf[i]) {
            if (mismatches < 16) {
                dev_err(&d->pdev->dev, "Loopback mismatch [%d]: wrote 0x%02x read 0x%02x\n",
                        i, write_buf[i], read_buf[i]);
            }
            mismatches++;
        }
    }

    if (mismatches == 0) {
        dev_info(&d->pdev->dev, "=== DMA LOOPBACK: SUCCESS ===\n");
        return 0;
    } else {
        dev_err(&d->pdev->dev, "=== DMA LOOPBACK: FAILED (%d mismatches) ===\n",
                mismatches);
        return -EIO;
    }
}

/**
 * c985_dma_test_vs_cpr - Test if DMA and CPR access same memory
 */
int c985_dma_test_vs_cpr(struct c985_poc *d)
{
    u32 test_addr = 0x10000;
    u32 pattern1 = 0xDEADBEEF;
    u32 pattern2 = 0x12345678;
    u32 cpr_read_val;
    u32 dma_read_val;
    int ret;

    dev_info(&d->pdev->dev, "=== DMA vs CPR TEST ===\n");

    /* Test 1: Write via CPR, read via DMA */
    dev_info(&d->pdev->dev, "Test 1: CPR write -> DMA read\n");
    cpr_write(d, test_addr, pattern1);

    /* Verify CPR can read it back */
    cpr_read(d, test_addr, &cpr_read_val);
    dev_info(&d->pdev->dev, "  CPR wrote 0x%08x, CPR reads 0x%08x\n",
             pattern1, cpr_read_val);

    /* Try DMA read */
    dma_read_val = 0;
    ret = c985_dma_read_sync(d, &dma_read_val, test_addr, 4);
    if (ret) {
        dev_err(&d->pdev->dev, "  DMA read failed: %d\n", ret);
    } else {
        dev_info(&d->pdev->dev, "  DMA reads 0x%08x\n", dma_read_val);
        if (dma_read_val == pattern1)
            dev_info(&d->pdev->dev, "  MATCH: DMA sees what CPR wrote\n");
        else
            dev_err(&d->pdev->dev, "  MISMATCH: DMA sees different data!\n");
    }

    /* Test 2: Write via DMA, read via CPR */
    dev_info(&d->pdev->dev, "Test 2: DMA write -> CPR read\n");
    ret = c985_dma_write_sync(d, &pattern2, test_addr + 4, 4);
    if (ret) {
        dev_err(&d->pdev->dev, "  DMA write failed: %d\n", ret);
        return ret;
    }

    /* Read via CPR */
    cpr_read(d, test_addr + 4, &cpr_read_val);
    dev_info(&d->pdev->dev, "  DMA wrote 0x%08x, CPR reads 0x%08x\n",
             pattern2, cpr_read_val);

    if (cpr_read_val == pattern2)
        dev_info(&d->pdev->dev, "  MATCH: CPR sees what DMA wrote\n");
    else
        dev_err(&d->pdev->dev, "  MISMATCH: CPR sees different data!\n");

    dev_info(&d->pdev->dev, "=== DMA vs CPR TEST COMPLETE ===\n");

    return 0;
}
