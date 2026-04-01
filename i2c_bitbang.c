// SPDX-License-Identifier: GPL-2.0
// i2c_bitbang.c — GPIO bit-bang I2C for AVerMedia C985

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "i2c_bitbang.h"

#define REG_GPIO_DIR    0x0610
#define REG_GPIO_VAL    0x0614
#define REG_GPIO_IN     0x0618

// CQLCodec_SetGPIOBitDirection + SetGPIODefaults

void gpio_drive_low(struct c985_poc *d, int pin)
{
    u32 dir = readl(d->bar1 + REG_GPIO_DIR);
    u32 val = readl(d->bar1 + REG_GPIO_VAL);

    dir |= BIT(pin);
    val &= ~BIT(pin);

    writel(dir, d->bar1 + REG_GPIO_DIR);
    writel(val, d->bar1 + REG_GPIO_VAL);
}

// CQLCodec_SetGPIOBitDirection
void gpio_release(struct c985_poc *d, int pin)
{
    u32 dir = readl(d->bar1 + REG_GPIO_DIR);
    dir &= ~BIT(pin);
    writel(dir, d->bar1 + REG_GPIO_DIR);
}

// CQLCodec_GetGPIOBitValue
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

void i2c_start(struct c985_poc *d, int scl, int sda)
{
    gpio_release(d, sda); i2c_delay();
    gpio_release(d, scl); i2c_delay();
    gpio_drive_low(d, sda); i2c_delay();
    gpio_drive_low(d, scl); i2c_delay();
}

void i2c_stop(struct c985_poc *d, int scl, int sda)
{
    gpio_drive_low(d, sda); i2c_delay();
    gpio_release(d, scl); i2c_delay();
    gpio_release(d, sda); i2c_delay();
}

int i2c_write(struct c985_poc *d, int scl, int sda, u8 byte)
{
    int i, ack;

    for (i = 7; i >= 0; i--) {
        if ((byte >> i) & 1)
            gpio_release(d, sda);
        else
            gpio_drive_low(d, sda);
        i2c_delay();
        gpio_release(d, scl); i2c_delay();
        gpio_drive_low(d, scl); i2c_delay();
    }

    gpio_release(d, sda); i2c_delay();
    gpio_release(d, scl); i2c_delay();
    ack = (gpio_read(d, sda) == 0) ? 1 : 0;
    gpio_drive_low(d, scl); i2c_delay();

    return ack;
}

//	HAL::getI2C_sw logic

u8 i2c_read(struct c985_poc *d, int scl, int sda, int send_ack)
{
    u8 val = 0;
    int i;

    gpio_release(d, sda);

    for (i = 7; i >= 0; i--) {
        gpio_release(d, scl);
        i2c_delay();
        if (gpio_read(d, sda))
            val |= (1 << i);
        gpio_drive_low(d, scl);
        i2c_delay();
    }

    if (send_ack)
        gpio_drive_low(d, sda);
    else
        gpio_release(d, sda);
    i2c_delay();
    gpio_release(d, scl); i2c_delay();
    gpio_drive_low(d, scl); i2c_delay();
    gpio_release(d, sda);

    return val;
}

int i2c_write_then_read(struct c985_poc *d, int scl, int sda, u8 addr7, u8 reg, u8 *buf, int len)
{
    int i;

    i2c_start(d, scl, sda);
    if (!i2c_write(d, scl, sda, addr7 << 1))
        goto fail;
    if (!i2c_write(d, scl, sda, reg))
        goto fail;

    i2c_start(d, scl, sda);
    if (!i2c_write(d, scl, sda, (addr7 << 1) | 1))
        goto fail;

    for (i = 0; i < len; i++)
        buf[i] = i2c_read(d, scl, sda, (i < len - 1) ? 1 : 0);

    i2c_stop(d, scl, sda);
    return 0;

    fail:
    i2c_stop(d, scl, sda);
    return -EIO;
}
