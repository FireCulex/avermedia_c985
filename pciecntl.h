/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PCIECNTL_H
#define PCIECNTL_H

#include "structs.h"

/*
 * DMA engine register offsets (from BAR1 + 0x810)
 * Each engine has its own 0x20 byte register block.
 */
#define DMA_ENGINE_SIZE         0x20
#define DMA_ENGINE_0_BASE       0x0810
#define DMA_ENGINE_1_BASE       (DMA_ENGINE_0_BASE + DMA_ENGINE_SIZE)

/*
 * DMA engine register offsets within each engine block
 */
#define DMA_REG_CAPABILITIES    0x00
#define DMA_REG_CONTROL_STATUS  0x04
#define DMA_REG_DESCRIPTOR_LO   0x08
#define DMA_REG_DESCRIPTOR_HI   0x0C
#define DMA_REG_HARDWARE_TIME   0x10
#define DMA_REG_CHAIN_BYTE_CNT  0x14

/*
 * DMA control/status bits (ControlStatus register)
 */
#define DMA_CTRL_START          BIT(0)      /* Start DMA transfer */
#define DMA_CTRL_STOP           BIT(1)      /* Stop DMA transfer */
#define DMA_CTRL_IRQ_EN         BIT(8)      /* Enable completion IRQ */
#define DMA_STATUS_BUSY         BIT(10)     /* DMA engine busy */
#define DMA_STATUS_COMPLETE     BIT(11)     /* Transfer complete */
#define DMA_STATUS_ERROR        BIT(12)     /* Transfer error */

/*
 * DMA descriptor control word bits
 */
#define DESC_CTRL_ENABLE        0x0000000F  /* Descriptor valid/enabled */
#define DESC_CTRL_MODE_0        0x0000000F  /* Mode 0: basic */
#define DESC_CTRL_MODE_3        0x0804200F  /* Mode 3: compressed video */

/*
 * Card address extended flags (upper bits of CardAddress field)
 */
#define CARD_ADDR_MODE_FLAG     BIT_ULL(34) /* Mode enable bit */
#define CARD_ADDR_LAST_DESC     BIT_ULL(59) /* Last descriptor in chain */

/*
 * DMA capabilities bits
 */
#define DMA_CAP_PRESENT         BIT(0)      /* DMA engine present */
#define DMA_CAP_DIRECTION_MASK  0x00000006  /* Direction capability */
#define DMA_CAP_DIR_S2C         0x00000000  /* System to Card only */
#define DMA_CAP_DIR_C2S         0x00000002  /* Card to System only */
#define DMA_CAP_DIR_BOTH        0x00000004  /* Bidirectional */

/*
 * Initialization / cleanup
 */
int pciecntl_init(struct c985_poc *d);
void pciecntl_cleanup(struct c985_poc *d);

/*
 * DMA operations
 */
int pciecntl_start_dma_read(struct c985_poc *d,
                            u32 card_addr,
                            void *host_buf,
                            u32 length,
                            bool sync);

int pciecntl_start_dma_write(struct c985_poc *d,
                             u32 card_addr,
                             const void *host_buf,
                             u32 length,
                             bool sync);

int pciecntl_read_card_ram(struct c985_poc *d,
                           u32 card_addr,
                           void *buf,
                           u32 length);

int pciecntl_write_card_ram(struct c985_poc *d,
                            u32 card_addr,
                            const void *buf,
                            u32 length);

/*
 * DMA completion handlers (called from IRQ)
 */
void pciecntl_dma_read_done(struct c985_poc *d);
void pciecntl_dma_write_done(struct c985_poc *d);

/*
 * ISR registration
 */
int CPCIeCntl_RegisterISR(struct c985_poc *d);
int CPCIeCntl_UnregisterISR(struct c985_poc *d);

/*
 * Helper to get DMA engine base address
 */
static inline void __iomem *pciecntl_engine_base(struct c985_poc *d, int engine)
{
    return c985_bar1(d) + DMA_ENGINE_0_BASE + (engine * DMA_ENGINE_SIZE);
}
u32 CPCIeCntl_GetMaxDMASize(struct c985_poc *param_1);

#endif /* PCIECNTL_H */
