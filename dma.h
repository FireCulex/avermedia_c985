// SPDX-License-Identifier: GPL-2.0
/*
 * dma.h - DMA engine for AVerMedia C985
 */

#ifndef _C985_DMA_H
#define _C985_DMA_H

#include <linux/types.h>

struct c985_poc;

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

#define MAX_DMA_CHANNELS        8

#define DESC_ALIGN              32
#define MAX_CHUNK_SIZE          (64 * 1024)  /* 64KB per descriptor */

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

/* Discovered DMA engine info */
struct dma_engine_info {
    int index;          /* Engine index 0-63 */
    u32 base;           /* Register base offset */
    u32 caps;           /* Capabilities register */
    bool is_c2s;        /* true=read (C2S), false=write (S2C) */
    int irq_line;       /* IRQ line number */
};

/* ========== Public API ========== */

/* Initialize and scan DMA engines */
int PedDmaInit(struct c985_poc *d);
void c985_dma_cleanup(struct c985_poc *d);

/* Synchronous DMA transfers */
int CQLCodec_StartDMAWrite(struct c985_poc *d, const void *src,
                        u32 card_addr, size_t size);
int CQLCodec_StartDMARead(struct c985_poc *d, void *dst,
                       u32 card_addr, size_t size);

/* Test functions */
int c985_dma_test_loopback(struct c985_poc *d);
int c985_dma_test_vs_cpr(struct c985_poc *d);

#endif /* _C985_DMA_H */
