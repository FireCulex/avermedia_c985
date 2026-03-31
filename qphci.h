/* SPDX-License-Identifier: GPL-2.0 */
#ifndef QPHCI_H
#define QPHCI_H

#include <linux/types.h>

struct c985_poc;

int qphci_init(struct c985_poc *d);
int qphci_reinit(struct c985_poc *d);
int qphci_init_arm_loop(struct c985_poc *d);
int dm_reset_arm(struct c985_poc *d, int run);

#endif
