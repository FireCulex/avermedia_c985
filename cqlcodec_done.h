// SPDX-License-Identifier: GPL-2.0
#ifndef _CQLCODEC_DONE_H
#define _CQLCODEC_DONE_H

#include "structs.h"

/* Main cleanup */
int CQLCodecLib_Done(struct c985_poc *d);

/* PCIe cleanup */
void CQLCodecLib_DonePCIe(struct c985_poc *d);
void CQLCodecLib_DeletePCIe(struct c985_poc *d);

/* Peripheral cleanup */
void CQLCodecLib_DonePeripherals(struct c985_poc *d);
void CQLCodecLib_DeletePeripherals(struct c985_poc *d);

/* PCI device cleanup */
int PciRemoveDevice(struct c985_poc *d);
void PciFlushQueuedRequests(struct c985_poc *d);
void PciWaitForRemoveEvent(struct c985_poc *d);
void PciReleaseResources(struct c985_poc *d);

/* Destructors */
void CPCIeCntl_Destructor(struct c985_poc *d);
void CQLCodec_Destructor(struct c985_poc *d);

/* PCI remove callback */
void c985_pci_remove(struct pci_dev *pdev);

#endif /* _CQLCODEC_DONE_H */
