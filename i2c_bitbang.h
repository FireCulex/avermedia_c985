/* SPDX-License-Identifier: GPL-2.0 */
#ifndef I2C_BITBANG_H
#define I2C_BITBANG_H

#include <linux/types.h>

struct c985_poc;

/* Bus 0: TI3101 */
#define I2C_SCL_TI3101    0
#define I2C_SDA_TI3101    2

/* Bus 1: NUC100 */
#define I2C_SCL_NUC100    14
#define I2C_SDA_NUC100    15

#define I2C_DELAY_US      5

/* Core I2C functions */
void i2c_start(struct c985_poc *d, int scl, int sda);
void i2c_stop(struct c985_poc *d, int scl, int sda);
int  i2c_write(struct c985_poc *d, int scl, int sda, u8 byte);
u8   i2c_read(struct c985_poc *d, int scl, int sda, int ack);
int  i2c_write_then_read(struct c985_poc *d, int scl, int sda, u8 addr7, u8 reg, u8 *buf, int len);

/* GPIO helpers */
void gpio_drive_low(struct c985_poc *d, int pin);
void gpio_release(struct c985_poc *d, int pin);

#endif
