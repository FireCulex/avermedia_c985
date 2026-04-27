#ifndef QPOSM_H
#define QPOSM_H

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/spinlock.h>

struct t_event_block;

void *QPOSMMalloc(unsigned long size);
void QPOSMFree(void *ptr);
int QPOSMGetAllocCount(void);

int QPOSMSetEvtgrp(void *evtgrp, u32 mask);
int QPOSMWaitEvtgrp(void *evtgrp, u32 mask, u32 *out_events, long timeout_ms);
void QPOSMCreateEvtgrp(struct t_event_block *evt);
int QPOSMClearEvtgrp(void *evtgrp, u32 mask);
int QPOSMDeleteEvtgrp(void *evtgrp);
int QPOSMDeleteSem(void *sem);
void QPOSMDeleteMutex(void *mutex);
int QPOSMLockMutex(void *mutex);
int QPOSMWaitSem(void *sem, int timeout_ms);
int QPOSMSignalSem(void *sem);

#endif /* QPOSM_H */