/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CPR_H
#define CPR_H

#include <linux/types.h>

struct c985_poc;

int CPR_MemoryWrite(struct c985_poc *d, u32 card_addr, u32 val);
int CPR_MemoryRead(struct c985_poc *d, u32 card_addr, u32 *out);

#endif
