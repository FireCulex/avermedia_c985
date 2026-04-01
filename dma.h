// SPDX-License-Identifier: GPL-2.0
/*
 * dma.h - DMA engine for AVerMedia C985
 */

#ifndef _C985_DMA_H
#define _C985_DMA_H

#include "avermedia_c985.h"

/* Initialize and scan DMA engines */
int c985_dma_init(struct c985_poc *d);
void c985_dma_cleanup(struct c985_poc *d);

/* Synchronous DMA transfers */
int c985_dma_write_sync(struct c985_poc *d, const void *src,
                        u32 card_addr, size_t size);
int c985_dma_read_sync(struct c985_poc *d, void *dst,
                       u32 card_addr, size_t size);

/* Test functions */
int c985_dma_test_loopback(struct c985_poc *d);
int c985_dma_test_vs_cpr(struct c985_poc *d);

#endif /* _C985_DMA_H */
