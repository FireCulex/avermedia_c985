/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TI3101_H
#define TI3101_H

#include <linux/types.h>

struct c985_poc;

#define TI3101_CHIP_ADDR      0x30
#define GPIO_TI3101_RST       6
#define TI3101_DEFAULT_VOLUME 6

void ti3101_hw_reset(struct c985_poc *d);
int  ti3101_probe(struct c985_poc *d);
int  ti3101_init(struct c985_poc *d);
int  ti3101_set_volume(struct c985_poc *d, u32 vol);
int  ti3101_read_reg(struct c985_poc *d, u8 reg, u8 *out);

#endif
