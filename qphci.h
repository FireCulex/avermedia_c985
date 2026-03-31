/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPHCI_H
#define QPHCI_H

#include <linux/types.h>

/* Register offsets for memory mapping windows */
#define REG_MEM_WIN_END     0x820
#define REG_MEM_WIN_OFFSET  0x824
#define REG_MEM_CTL         0x840

/* Page size for memory windows */
#define QPHCI_PAGE_SIZE     0x100000

/* Register offsets for memory mapping windows */
#define REG_MEM_WIN_BASE    0x81C
#define REG_MEM_WIN_START   0x81C
#define REG_MEM_WIN_END     0x820
#define REG_MEM_WIN_OFFSET  0x824
#define REG_MEM_CTL         0x840

/* ARM control registers (from DM_ResetArm) */
#define REG_ARM_CTRL        0x00
#define REG_ARM_RESET       0x10
#define REG_ARM_TIMER_VAL   0x18
#define REG_ARM_TIMER_CFG   0x1C
#define REG_ARM_BOOT        0x80C
#define REG_ARM_STATUS      0x800
#define REG_ARM_MAILBOX     0x6CC

/* Page size for memory windows */
#define QPHCI_PAGE_SIZE     0x100000

struct c985_poc;

int qphci_init(struct c985_poc *d);
int qphci_reinit(struct c985_poc *d);
int qphci_init_arm_loop(struct c985_poc *d);
int dm_reset_arm(struct c985_poc *d, int run);

#endif
