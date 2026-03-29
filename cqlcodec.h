/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CQLCODEC_H
#define CQLCODEC_H

#include <linux/types.h>

struct c985_poc;

/* Bus types */
#define QPHCI_BUS_USB           0
#define QPHCI_BUS_PCI           1
#define QPHCI_BUS_201_EMULATION 2

/* Encoder register offsets (chip_type & 0xe != 0) */
#define ENC_REG_MESSAGE             0x6fc
#define ENC_REG_SYSTEM_CONTROL      0x6f8
#define ENC_REG_PICTURE_RESOLUTION  0x6f4
#define ENC_REG_INPUT_CONTROL       0x6f0
#define ENC_REG_RATE_CONTROL        0x6ec
#define ENC_REG_BIT_RATE            0x6e8
#define ENC_REG_FILTER_CONTROL      0x6e4
#define ENC_REG_GOP_LOOP_FILTER     0x6e0
#define ENC_REG_OUT_PIC_RESOLUTION  0x6dc
#define ENC_REG_ET_CONTROL          0x6d8
#define ENC_REG_BLOCK_SIZE          0x6d8
#define ENC_REG_AUDIO_CONTROL_PARAM 0x6d4
#define ENC_REG_AUDIO_CONTROL_EX    0x6d0

/* Default control values */
#define DEFAULT_AO_CONTROLS     0x17  /* bits 0,1,2,4 set; bits 3,5,6 clear */
#define DEFAULT_AUD_CONTROLS    0x1202

/* Public API */
int  cqlcodec_init_device(struct pci_dev *pdev, const struct pci_device_id *id);
void cqlcodec_remove_device(struct pci_dev *pdev);

void cqlcodec_ao_switch(struct c985_poc *d, int disable);
void cqlcodec_vo_switch(struct c985_poc *d, int disable);
void cqlcodec_load_default_settings(struct c985_poc *d);
int  cqlcodec_register_isr(struct c985_poc *d);
int cqlcodec_fw_download(struct c985_poc *d, int do_reset);
#endif
