// SPDX-License-Identifier: GPL-2.0
// avermedia_c985.c — PCI driver wrapper for AVerMedia C985

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>


#include "avermedia_c985.h"
#include "cqlcodec.h"
#include "ti3101.h"
#include "nuc100.h"
#include "project.h"
#include "v4l2.h"

MODULE_DESCRIPTION(DRV_DESC);

static void hci_work_handler(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, irq_work);
    dev_dbg(&d->pdev->dev, "HCI work handler called\n");
    /* TODO: Process ARM messages */
}

static void dma_work_handler(struct work_struct *work)
{
    struct c985_poc *d = container_of(work, struct c985_poc, dma_work);
    dev_dbg(&d->pdev->dev, "DMA work handler called, status=0x%08x\n",
            d->dma_interrupt_status);
    /* TODO: Process DMA completions, clear bits from dma_interrupt_status */
    d->dma_interrupt_status = 0;
}

// CDevice::Init
static int c985_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct c985_poc *d;
    int ret;
    int i;

    /* Fix PCI class code for proper device identification */
    pdev->class = PCI_CLASS_MULTIMEDIA_VIDEO << 8;

    ret = cqlcodec_init_device(pdev, id);
    if (ret)
        return ret;

    d = pci_get_drvdata(pdev);

    /* Scan for available DMA channels (from PedDmaInit) */
    d->num_dma_channels = 0;
    for (i = 0; i < 64; i++) {
        u32 caps = readl(d->bar0 + (i * 0x100));  /* PED_DMA_ENGINE.Capabilities */
        if (caps & 0x01) {
            d->num_dma_channels++;
        }
    }
    dev_info(&pdev->dev, "Found %d DMA channels\n", d->num_dma_channels);

    ret = cqlcodec_fw_download(d, 1);
    if (ret)
        goto err_remove;

    ret = project_c985_init(d);
    if (ret)
        goto err_remove;

    /* Register V4L2 */
    ret = c985_v4l2_register(d);
    if (ret)
        goto err_remove;

    return 0;

    err_remove:
    cqlcodec_remove_device(pdev);
    return ret;
}

static void c985_pci_remove(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);

    if (d) {
        c985_v4l2_unregister(d);
    }
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
