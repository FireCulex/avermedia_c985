/* SPDX-License-Identifier: GPL-2.0 */
#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include <linux/types.h>

struct c985_poc;

/* GPIO pin assignments */
#define I2C_SCL_GPIO    0
#define I2C_SDA_GPIO    2
#define I2C_DELAY_US    5

/* Primitives */
void i2c_bb_start(struct c985_poc *d);
void i2c_bb_stop(struct c985_poc *d);
int  i2c_bb_write_byte(struct c985_poc *d, u8 byte);  /* returns 1=ACK, 0=NAK */
u8   i2c_bb_read_byte(struct c985_poc *d, int ack);   /* ack=1 send ACK, 0=NAK */

/* GPIO helpers (exposed for direct use if needed) */
void i2c_bb_sda_low(struct c985_poc *d);
void i2c_bb_sda_high(struct c985_poc *d);
void i2c_bb_scl_low(struct c985_poc *d);
void i2c_bb_scl_high(struct c985_poc *d);
int  i2c_bb_sda_read(struct c985_poc *d);

#endif
