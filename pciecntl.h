/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PCIECNTL_H
#define PCIECNTL_H

#include <linux/types.h>
#include <linux/completion.h>

struct c985_poc;

/* Initialize/cleanup */
int pciecntl_init(struct c985_poc *d);
void pciecntl_cleanup(struct c985_poc *d);

/* DMA operations */
int pciecntl_start_dma_read(struct c985_poc *d,
                            u32 card_addr,
                            void *host_buf,
                            u32 length,
                            bool sync);

/* IRQ handlers */
void pciecntl_dma_read_done(struct c985_poc *d);
void pciecntl_dma_write_done(struct c985_poc *d);

/* Utility */
int pciecntl_read_card_ram(struct c985_poc *d,
                           u32 card_addr,
                           void *buf,
                           u32 length);

#endif /* PCIECNTL_H */
