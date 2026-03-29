// SPDX-License-Identifier: GPL-2.0
// avermedia_c985.c — PCI driver wrapper for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "cqlcodec.h"

#define DRV_NAME "avermedia_c985_poc"

static int c985_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    return cqlcodec_init_device(pdev, id);
}

static void c985_pci_remove(struct pci_dev *pdev)
{
    cqlcodec_remove_device(pdev);
}

static const struct pci_device_id c985_pci_ids[] = {
    { PCI_DEVICE(0x1af2, 0xa001) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, c985_pci_ids);

static struct pci_driver c985_pci_driver = {
    .name     = DRV_NAME,
    .id_table = c985_pci_ids,
    .probe    = c985_pci_probe,
    .remove   = c985_pci_remove,
};

module_pci_driver(c985_pci_driver);

MODULE_DESCRIPTION("AVerMedia C985 PoC driver");
MODULE_AUTHOR("fireculex@gmail.com");
MODULE_LICENSE("GPL");
