
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NUC100_H
#define NUC100_H

#include <linux/types.h>

/* I2C addresses and GPIO definitions */
#define NUC100_I2C_ADDR       0x2b   /* C985McuAddr */
#define NUC100_RST_GPIO       0x05   /* C985McuRst */

#define TI3101_I2C_ADDR       0x30   /* C985AlgAudAddr */
#define TI3101_RST_GPIO       0x06   /* C985AlgAudRst */

#define AUD_SWITCH_GPIO1      0x0d   /* C985AudSwitch1 */
#define AUD_SWITCH_GPIO2      0x07   /* C985AudSwitch2 */

struct c985_poc;

/* HDMI timing information structure */
struct nuc100_hdmi_timing {
    u16 hactive;
    u16 vactive;
    u16 htotal;
    u16 vtotal;
    u32 pixelclock;   /* in Hz */
    u8 hpol;
    u8 vpol;
};

/* Parameter structure for register access via NUC100 */
struct nuc100_params {
    u8 command;     /* 0 = read, 1 = write */
    u8 chip;        /* downstream chip address */
    u8 reg_address; /* register on downstream chip */
    u8 data[4];     /* data buffer */
};

/* Function declarations */
int nuc100_init(struct c985_poc *d);
int nuc100_check_device(struct c985_poc *d);
int nuc100_read_reg(struct c985_poc *d, u8 reg, u8 *val);
int nuc100_get_hdmi_status(struct c985_poc *d);
int nuc100_get_hdmi_timing(struct c985_poc *d,
                           struct nuc100_hdmi_timing *t,
                           int *valid);
int nuc100_access_regs(struct c985_poc *d, struct nuc100_params *p);

#endif /* NUC100_H */

