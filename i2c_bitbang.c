// SPDX-License-Identifier: GPL-2.0
// i2c_bitbang.c — GPIO bit-bang I2C primitives for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"

#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614
#define REG_GPIO_IN     0x0618

/* --- GPIO primitives (open-drain) --- */

static void gpio_drive_low(struct c985_poc *d, int pin)
{
    u32 dir = readl(d->bar1 + REG_GPIO_DIR);
    u32 val = readl(d->bar1 + REG_GPIO_VAL);

    dir |= BIT(pin);
    val &= ~BIT(pin);

    writel(dir, d->bar1 + REG_GPIO_DIR);
    writel(val, d->bar1 + REG_GPIO_VAL);
}

static void gpio_release(struct c985_poc *d, int pin)
{
    u32 dir = readl(d->bar1 + REG_GPIO_DIR);
    dir &= ~BIT(pin);
    writel(dir, d->bar1 + REG_GPIO_DIR);
}

static int gpio_read(struct c985_poc *d, int pin)
{
    u32 dir = readl(d->bar1 + REG_GPIO_DIR);
    dir &= ~BIT(pin);
    writel(dir, d->bar1 + REG_GPIO_DIR);
    return (readl(d->bar1 + REG_GPIO_IN) & BIT(pin)) ? 1 : 0;
}

static inline void i2c_delay(void)
{
    udelay(I2C_DELAY_US);
}

/* --- Public signal helpers --- */

void i2c_bb_sda_low(struct c985_poc *d)  { gpio_drive_low(d, I2C_SDA_GPIO); }
void i2c_bb_sda_high(struct c985_poc *d) { gpio_release(d, I2C_SDA_GPIO); }
void i2c_bb_scl_low(struct c985_poc *d)  { gpio_drive_low(d, I2C_SCL_GPIO); }
void i2c_bb_scl_high(struct c985_poc *d) { gpio_release(d, I2C_SCL_GPIO); }
int  i2c_bb_sda_read(struct c985_poc *d) { return gpio_read(d, I2C_SDA_GPIO); }

/* --- Bus primitives --- */

void i2c_bb_start(struct c985_poc *d)
{
    i2c_bb_sda_high(d); i2c_delay();
    i2c_bb_scl_high(d); i2c_delay();
    i2c_bb_sda_low(d);  i2c_delay();
    i2c_bb_scl_low(d);  i2c_delay();
}

void i2c_bb_stop(struct c985_poc *d)
{
    i2c_bb_sda_low(d);  i2c_delay();
    i2c_bb_scl_high(d); i2c_delay();
    i2c_bb_sda_high(d); i2c_delay();
}

int i2c_bb_write_byte(struct c985_poc *d, u8 byte)
{
    int i, ack;

    for (i = 7; i >= 0; i--) {
        if ((byte >> i) & 1)
            i2c_bb_sda_high(d);
        else
            i2c_bb_sda_low(d);
        i2c_delay();
        i2c_bb_scl_high(d); i2c_delay();
        i2c_bb_scl_low(d);  i2c_delay();
    }

    i2c_bb_sda_high(d); i2c_delay();
    i2c_bb_scl_high(d); i2c_delay();
    ack = (i2c_bb_sda_read(d) == 0) ? 1 : 0;
    i2c_bb_scl_low(d);  i2c_delay();

    return ack;
}

u8 i2c_bb_read_byte(struct c985_poc *d, int send_ack)
{
    u8 val = 0;
    int i;

    i2c_bb_sda_high(d);

    for (i = 7; i >= 0; i--) {
        i2c_bb_scl_high(d);
        i2c_delay();
        if (i2c_bb_sda_read(d))
            val |= (1 << i);
        i2c_bb_scl_low(d);
        i2c_delay();
    }

    /* ACK/NAK */
    if (send_ack)
        i2c_bb_sda_low(d);
    else
        i2c_bb_sda_high(d);
    i2c_delay();
    i2c_bb_scl_high(d); i2c_delay();
    i2c_bb_scl_low(d);  i2c_delay();
    i2c_bb_sda_high(d);

    return val;
}
