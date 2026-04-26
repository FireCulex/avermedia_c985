#ifndef QPMM_H
#define QPMM_H

#include <linux/types.h>

void *QPMMMalloc2Ex(unsigned long size);
void QPMMFree2Ex(void *ptr);
int QPMMGetAllocCount(void);


#endif /* QPMM_H */
