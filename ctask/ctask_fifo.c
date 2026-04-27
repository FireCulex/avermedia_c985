// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_fifo.c - FIFO operations
 */

#include "ctask_private.h"

/* ================================================================
 * CFifo_Constructor
 * ================================================================ */
struct c_fifo *CFifo_Constructor(struct c_fifo *fifo, struct CObject *parent,
                                 u32 attr, u32 size, u32 entry_size)
{
    if (!fifo)
        return NULL;

    CObject_Constructor(&fifo->m_Object, parent, attr);

    fifo->m_Fifo = kzalloc(size * entry_size, GFP_KERNEL);
    if (!fifo->m_Fifo)
        return NULL;

    fifo->m_dwReadPtr = 0;
    fifo->m_dwWritePtr = 0;
    fifo->m_size = size;
    fifo->m_sizeEntry = entry_size;
    fifo->m_dwFifoLevel = 0;

    return fifo;
}

/* ================================================================
 * CFifo_Destructor
 * ================================================================ */
void CFifo_Destructor(struct c_fifo *fifo)
{
    if (!fifo)
        return;

    kfree(fifo->m_Fifo);
    fifo->m_Fifo = NULL;
    fifo->m_dwReadPtr = 0;
    fifo->m_dwWritePtr = 0;
    fifo->m_dwFifoLevel = 0;
}

/* ================================================================
 * CFifo_GetFifo - Read entry from FIFO
 * ================================================================ */
int CFifo_GetFifo(struct c_fifo *fifo, void *entry)
{
    if (!fifo || !entry || !fifo->m_Fifo)
        return 0;

    CObject_lock(&fifo->m_Object);

    if (fifo->m_dwFifoLevel == 0) {
        CObject_unlock(&fifo->m_Object);
        return 0;
    }

    memcpy(entry, fifo->m_Fifo + (fifo->m_dwReadPtr * fifo->m_sizeEntry),
           fifo->m_sizeEntry);

    fifo->m_dwReadPtr = (fifo->m_dwReadPtr + 1) % fifo->m_size;
    fifo->m_dwFifoLevel--;

    CObject_unlock(&fifo->m_Object);

    return 1;
}

/* ================================================================
 * CFifo_SetFifo - Write entry to FIFO
 * ================================================================ */
int CFifo_SetFifo(struct c_fifo *fifo, void *entry)
{
    if (!fifo || !entry || !fifo->m_Fifo)
        return 0;

    CObject_lock(&fifo->m_Object);

    if (fifo->m_dwFifoLevel >= fifo->m_size) {
        CObject_unlock(&fifo->m_Object);
        return 0;
    }

    memcpy(fifo->m_Fifo + (fifo->m_dwWritePtr * fifo->m_sizeEntry),
           entry, fifo->m_sizeEntry);

    fifo->m_dwWritePtr = (fifo->m_dwWritePtr + 1) % fifo->m_size;
    fifo->m_dwFifoLevel++;

    CObject_unlock(&fifo->m_Object);

    return 1;
}

/* ================================================================
 * CFifo_GetFifoLevel
 * ================================================================ */
u32 CFifo_GetFifoLevel(struct c_fifo *fifo)
{
    if (!fifo)
        return 0;

    return fifo->m_dwFifoLevel;
}
