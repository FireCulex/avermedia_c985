/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QL201_I2C_H
#define QL201_I2C_H

#include <linux/types.h>

struct c985_poc;

int ql201_i2c_write(struct c985_poc *d, u8 addr, const u8 *buf, int len);
int ql201_i2c_write_read(struct c985_poc *d, u8 addr,
                         const u8 *wbuf, int wlen,
                         u8 *rbuf, int rlen);
int ql201_i2c_debug_ping(struct c985_poc *d, u8 addr);

#endif
