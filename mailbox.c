// SPDX-License-Identifier: GPL-2.0
// mailbox.c — ARM firmware mailbox for AVerMedia C985
//
// The ARM owns GPIO and I2C once it is running. All such operations
// must go through this mailbox; direct MMIO access to those registers
// from the host races against / is ignored by the running firmware.
//
// Protocol (from QPFWAPI_SendMessageToARM + QPFWAPI_MailboxReady REFS):
//
//   Handshake register: REG_TO_ARM_MSG_STATUS (0x6CC)
//     bit[0] == 1  -> mailbox busy (ARM hasn't consumed previous command)
//     bit[0] == 0  -> mailbox ready for next command
//
//   To send a command:
//     1. Poll 0x6CC until bit[0] == 0  (MailboxReady)
//     2. Write 0x6CC: (payload & 0xffff0000) | (param4_flag << 8) | 1
//     3. Write 0x6FC: full 32-bit payload
//     4. Write 0x6C8 = 1  (SetInterrupt — kick ARM)
//
//   The ARM processes the command and clears bit[0] of 0x6CC when done.
//
// NOTE: The task_id / property-set encoding in the upper bits of the
// status word (0x6CC bits[31:16]) is still TBD — we need the
// PROPSETID_QPCODEC_CONTROL dispatcher decompile to confirm what the
// ARM expects there.  For now we use (payload & 0xffff0000) which is
// what QPFWAPI_SendMessageToARM does for our chip type.

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "mailbox.h"

/* Host device context — defined in avermedia_c985.c */
struct c985_poc {
    struct pci_dev  *pdev;
    void __iomem    *bar1;
    u32              chip_ver;
};

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

static inline u32 reg_rd(struct c985_poc *d, u32 off)
{
    return readl(d->bar1 + off);
}

static inline void reg_wr(struct c985_poc *d, u32 off, u32 val)
{
    writel(val, d->bar1 + off);
}

/**
 * mb_wait_ready() - Poll 0x6CC until bit[0] clears (ARM consumed last cmd).
 *
 * From QPFWAPI_MailboxReady: loops reading 0x6CC, signals/waits semaphore
 * between iterations, exits when (val & 1) == 0.  In kernel space we just
 * spin with a timeout — no semaphore needed since probe is single-threaded.
 *
 * Timeout matches Windows driver: param_2 is passed in ms, typically 500.
 */
static int mb_wait_ready(struct c985_poc *d, unsigned int timeout_ms)
{
    unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
    u32 val;

    do {
        val = reg_rd(d, REG_TO_ARM_MSG_STATUS);
        if (!(val & 1))
            return 0;

        if (time_after(jiffies, deadline)) {
            dev_err(&d->pdev->dev,
                    "MB: MailboxReady timeout (0x6CC=0x%08x)\n", val);
            return -ETIMEDOUT;
        }

        udelay(100);
    } while (1);
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/**
 * mb_send() - Send one mailbox command to the ARM and wait for it to be
 *             consumed before returning.
 *
 * status_word (0x6CC) layout for our chip type ((m_ChipType & 0xe) != 0):
 *   bits[31:16] = payload[31:16]   (upper half of payload word)
 *   bit [8]     = 0                (param_4 / response-needed flag)
 *   bit [0]     = 1                (valid / start)
 *
 * Sequence:
 *   1. Poll 0x6CC bit[0] == 0     (wait for ARM to be idle)
 *   2. Write 0x6CC = status_word
 *   3. Write 0x6FC = payload
 *   4. Write 0x6C8 = 1            (SetInterrupt)
 *   5. Poll 0x6CC bit[0] == 0     (wait for ARM to clear it = done)
 */
int mb_send(struct c985_poc *d, u32 task_id, u32 payload)
{
    u32 status_word;
    int ret;

    /* Step 1: wait for mailbox to be free */
    ret = mb_wait_ready(d, 500);
    if (ret)
        return ret;

    status_word = (payload & 0xffff0000) | 0x0001; /* param4=0, valid=1 */

    dev_dbg(&d->pdev->dev,
            "MB: task=0x%02x payload=0x%08x status=0x%08x\n",
            task_id, payload, status_word);

    /* Steps 2-4: write status, payload, kick */
    reg_wr(d, REG_TO_ARM_MSG_STATUS, status_word);
    reg_wr(d, REG_TO_ARM_MSG,        payload);
    reg_wr(d, REG_ARM_INTERRUPT,     1);

    /* Step 5: wait for ARM to clear bit[0] — command consumed */
    ret = mb_wait_ready(d, 500);
    if (ret)
        dev_err(&d->pdev->dev,
                "MB: ARM did not consume command (task=0x%02x payload=0x%08x)\n",
                task_id, payload);

        return ret;
}

/**
 * mb_gpio_set_dir() - Set a GPIO pin direction via the ARM mailbox.
 *
 * Payload format (prop 0x3c, from CDevice::SetGPIO + TI3101::HwReset):
 *   byte0 = gpio_num
 *   byte1 = 1 (output) / 0 (input)
 *   byte2 = 0
 *   byte3 = 0
 */
int mb_gpio_set_dir(struct c985_poc *d, int pin, int out)
{
    u32 payload = ((u32)(out & 1) << 8) | (u32)(pin & 0xff);

    dev_dbg(&d->pdev->dev, "MB: GPIO%d dir=%d\n", pin, out);
    return mb_send(d, MB_TASK_GPIO_DIR, payload);
}

/**
 * mb_gpio_set_val() - Set a GPIO output value via the ARM mailbox.
 *
 * Payload format (prop 0x3d, from CDevice::SetGPIO + TI3101::HwReset):
 *   byte0 = gpio_num
 *   byte1 = value (0 or 1)
 *   byte2 = 0
 *   byte3 = 0
 *
 * Note: CDevice::SetGPIO always sets direction first, then waits 5ms,
 * then sets value. ti3101_hw_reset() mirrors that exact timing.
 */
int mb_gpio_set_val(struct c985_poc *d, int pin, int val)
{
    u32 payload = ((u32)(val & 1) << 8) | (u32)(pin & 0xff);

    dev_dbg(&d->pdev->dev, "MB: GPIO%d val=%d\n", pin, val);
    return mb_send(d, MB_TASK_GPIO_VAL, payload);
}

/**
 * mb_i2c_write() - Write one register via the ARM firmware's SW I2C engine.
 *
 * Payload format (_QPCODEC_DIAG_I2C_STRUCT, prop 0x23,
 *                 from TI3101::WriteRegister / HAL::setI2C_sw):
 *   byte0 = chip_addr7  (7-bit slave address)
 *   byte1 = reg
 *   byte2 = data
 *   byte3 = num_bytes (2)
 *
 * Note: WriteRegister stores the 8-bit write byte (0x60) in m_slave_address
 * and right-shifts it by 1 before filling the struct, yielding 0x30.
 * We take the 7-bit address directly.
 */
int mb_i2c_write(struct c985_poc *d, u8 chip_addr7, u8 reg, u8 data)
{
    u32 payload = ((u32)2          << 24) |
    ((u32)data       << 16) |
    ((u32)reg        <<  8) |
    ((u32)chip_addr7);

    dev_dbg(&d->pdev->dev,
            "MB: I2C addr=0x%02x reg=0x%02x data=0x%02x\n",
            chip_addr7, reg, data);
    return mb_send(d, MB_TASK_SW_I2C, payload);
}
