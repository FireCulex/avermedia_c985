/* SPDX-License-Identifier: GPL-2.0 */
/* mailbox.h — ARM firmware mailbox interface for AVerMedia C985
 *
 * The running ARM firmware owns the GPIO and I2C hardware.
 * The host must not poke those registers directly once the ARM is up.
 * All GPIO and I2C operations go through the firmware mailbox.
 *
 * Register map (BAR1 offsets, from QPFWAPI_SendMessageToARM REFS):
 *
 *   0x6CC  REG_TO_ARM_MSG_STATUS — written first:
 *            bits[31:16] = upper 16 bits of message payload (for our chip type)
 *            bits[15: 9] = 0
 *            bit [    8] = param_4 flag (0 for fire-and-forget)
 *            bit [    0] = 1 (start/valid)
 *
 *   0x6FC  REG_TO_ARM_MSG       — written second: full 32-bit message payload
 *
 *   After both writes, SetInterrupt(1) is issued via 0x6C8 (see below).
 *
 *   0x6C8  REG_ARM_INTERRUPT    — write 1 to kick the ARM
 *
 * Mailbox handshake registers (from QPFWAPI_MailboxReady / MailboxDone REFS):
 *   TBD — need those decompiles to fill in precisely.
 *
 * Message payload format (_ARM_MESSAGE.Read, u32):
 *
 *   GPIO direction  (prop 0x3c):  0x00000100 | gpio_num
 *     — byte0=gpio_num, byte1=1(output)/0(input), byte2=0, byte3=0
 *
 *   GPIO value      (prop 0x3d):  (value << 8) | gpio_num
 *     — byte0=gpio_num, byte1=value(0|1), byte2=0, byte3=0
 *
 *   SW I2C write    (prop 0x23):  (num_bytes << 24) | (data << 16) | (reg << 8) | chip_addr7
 *     — chip_addr7 = 7-bit slave address (0x30 for TI3101)
 *     — reg        = TI3101 register address
 *     — data       = value to write
 *     — num_bytes  = 2 (reg + data)
 *
 * Task IDs (param_2 to SendMessageToARM) — to be confirmed from dispatcher decompile:
 *   GPIO_DIR_TASK   — property 0x3c handler
 *   GPIO_VAL_TASK   — property 0x3d handler
 *   SW_I2C_TASK     — property 0x23 handler
 */

#ifndef MAILBOX_H
#define MAILBOX_H

#include <linux/types.h>

struct c985_poc;

/* -----------------------------------------------------------------------
 * BAR1 register offsets
 * --------------------------------------------------------------------- */
#define REG_TO_ARM_MSG_STATUS   0x06cc
#define REG_TO_ARM_MSG          0x06fc
#define REG_ARM_INTERRUPT       0x06c8  /* write 1 = kick ARM */

/* -----------------------------------------------------------------------
 * Task IDs — placeholders until dispatcher decompile confirms values.
 * Naming follows the PROPSETID_QPCODEC_CONTROL / DIAG property numbers.
 * --------------------------------------------------------------------- */
#define MB_TASK_GPIO_DIR        0x3c    /* set GPIO direction */
#define MB_TASK_GPIO_VAL        0x3d    /* set GPIO output value */
#define MB_TASK_SW_I2C          0x23    /* software I2C write (DIAG) */

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

/**
 * mb_send() - Fire a single mailbox message to the ARM (no ACK wait).
 * @d:       device context
 * @task_id: firmware task/property ID (MB_TASK_*)
 * @payload: 32-bit message word packed per the format above
 *
 * Mirrors the non-blocking path in QPFWAPI_SendMessageToARM (param_6==0).
 * Returns 0 on success, -EIO on register write failure.
 */
int mb_send(struct c985_poc *d, u32 task_id, u32 payload);

/**
 * mb_gpio_set_dir() - Set a GPIO pin direction via the ARM mailbox.
 * @d:    device context
 * @pin:  GPIO number
 * @out:  1 = output, 0 = input
 */
int mb_gpio_set_dir(struct c985_poc *d, int pin, int out);

/**
 * mb_gpio_set_val() - Set a GPIO output value via the ARM mailbox.
 * @d:    device context
 * @pin:  GPIO number
 * @val:  1 = high, 0 = low
 */
int mb_gpio_set_val(struct c985_poc *d, int pin, int val);

/**
 * mb_i2c_write() - Write one register via the ARM firmware's SW I2C engine.
 * @d:          device context
 * @chip_addr7: 7-bit slave address (e.g. 0x30 for TI3101)
 * @reg:        register address byte
 * @data:       data byte to write
 */
int mb_i2c_write(struct c985_poc *d, u8 chip_addr7, u8 reg, u8 data);

#endif /* MAILBOX_H */
