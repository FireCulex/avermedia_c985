/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NUC100_H
#define NUC100_H

/* nuc100.h or avermedia_c985.h */
#define NUC100_I2C_ADDR       0x2b   /* C985McuAddr */
#define NUC100_RST_GPIO       0x05   /* C985McuRst */

#define TI3101_I2C_ADDR       0x30   /* C985AlgAudAddr - matches your ti3101.h */
#define TI3101_RST_GPIO       0x06   /* C985AlgAudRst - matches your ti3101.h */

#define AUD_SWITCH_GPIO1      0x0d   /* C985AudSwitch1 */
#define AUD_SWITCH_GPIO2      0x07   /* C985AudSwitch2 */

struct c985_poc;

int nuc100_get_hdmi_timing(struct c985_poc *d);
int nuc100_init(struct c985_poc *d);
int nuc100_check_device(struct c985_poc *d);

#endif
