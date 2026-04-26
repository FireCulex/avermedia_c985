#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/module.h>
#include "qpmm.h"

/* Tracks active allocations to help detect leaks */
static atomic_t s_alloc_count = ATOMIC_INIT(0);

void *QPMMMalloc2Ex(unsigned long size)
{
    void *ptr;

    /* kzalloc matches the decompiled behavior: allocate and zero memory */
    ptr = kzalloc(size, GFP_KERNEL);

    if (ptr) {
        atomic_inc(&s_alloc_count);
        pr_debug("QPMMMalloc2Ex() ptr(%p) size(%lu) cnt(%d)\n",
                 ptr, size, atomic_read(&s_alloc_count));
    } else {
        pr_debug("QPMMMalloc2Ex() FAILED size(%lu)\n", size);
    }

    return ptr;
}

void QPMMFree2Ex(void *ptr)
{
    if (ptr) {
        atomic_dec(&s_alloc_count);
        pr_debug("QPMMFree2Ex() ptr(%p) cnt(%d)\n",
                 ptr, atomic_read(&s_alloc_count));
        kfree(ptr);
    }
}

int QPMMGetAllocCount(void)
{
    return atomic_read(&s_alloc_count);
}

