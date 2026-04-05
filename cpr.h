/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CPR_H
#define CPR_H

#include <linux/types.h>

#define cpr_write CPR_MemoryWrite
#define cpr_read CPR_MemoryRead

struct c985_poc;

int CPR_MemoryWrite(struct c985_poc *d, u32 param_2, u32 param_3);

int CPR_MemoryRead(struct c985_poc *d, u32 param_2, u32 *param_3);

#endif
