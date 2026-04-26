// SPDX-License-Identifier: GPL-2.0
/*
 * cqlcodec_done.c - AVerMedia C985 cleanup/shutdown functions
 */

#include <linux/pci.h>
#include <linux/io.h>
#include <linux/workqueue.h>

#include "structs.h"
#include "avermedia_c985.h"
#include "cqlcodec.h"
#include "pciecntl.h"
#include "interrupts.h"
#include "v4l2.h"
#include "qpfwencapi.h"
#include <linux/delay.h>
#include "cobject.h"
#include "ctask/ctask.h"
#include "qpmm.h"
/**
 * PciReleaseResources - Release all PCI/DMA resources
 * @d: device structure
 *
 * From decompile: unmaps memory, frees DMA buffers, unregisters ISR
 */
void PciReleaseResources(struct c985_poc *d)
{
    int i;

    dev_dbg(&d->pdev->dev, "PciReleaseResources()\n");

    /* Free DMA descriptors for all channels */
    for (i = 0; i < 64; i++) {
        struct device_transfer *dma_dev = &d->pcie.DmaDevices[i];

        if (dma_dev->DmaDescriptor.VirtAddress != NULL) {
            dma_free_coherent(&d->pdev->dev,
                              dma_dev->NumDescriptors * 0x20 + 0x20,
                              dma_dev->DmaDescriptor.VirtAddress,
                              dma_dev->DmaDescriptor.PhysAddress);
            dma_dev->DmaDescriptor.VirtAddress = NULL;
            dma_dev->DmaDescriptor.PhysAddress = 0;
            dma_dev->NumDescriptors = 0;
            dma_dev->bStarted = 0;
        }
    }

    /* Unregister ISR */
    CPCIeCntl_UnregisterISR(d);

    /* Unmap memory ranges */
    if (d->pcie.pRegistersEx) {
        iounmap(d->pcie.pRegistersEx);
        d->pcie.pRegistersEx = NULL;
    }

    if (d->pcie.pRegisters) {
        iounmap(d->pcie.pRegisters);
        d->pcie.pRegisters = NULL;
    }
}

/**
 * PciWaitForRemoveEvent - Wait for pending operations to complete
 * @d: device structure
 */
void PciWaitForRemoveEvent(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "PciWaitForRemoveEvent()\n");

    /* Wait for any pending DMA to complete */
    if (d->dma_active) {
        unsigned long timeout = msecs_to_jiffies(1000);
        wait_for_completion_timeout(&d->dma_done, timeout);
    }
}

/**
 * PciFlushQueuedRequests - Flush any pending requests
 * @d: device structure
 */
void PciFlushQueuedRequests(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "PciFlushQueuedRequests()\n");

    /* Cancel pending work */
    cancel_work_sync(&d->irq_work);
    cancel_work_sync(&d->dma_work);
}

/**
 * PciRemoveDevice - Remove PCI device
 * @d: device structure
 *
 * Returns: 0 on success
 */
int PciRemoveDevice(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "PciRemoveDevice()\n");

    if (d == NULL)
        return 0;

    /* Mark device as stopping */
    d->pcie.State = 0;

    PciFlushQueuedRequests(d);
    PciWaitForRemoveEvent(d);
    PciReleaseResources(d);

    return 0;
}

/**
 * CPCIeCntl_Destructor - Destructor for PCIe controller
 * @d: device structure
 */
void CPCIeCntl_Destructor(struct c985_poc *d)
{
    int ret;

    dev_dbg(&d->pdev->dev, "CPCIeCntl_Destructor()\n");

    ret = PciRemoveDevice(d);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "CPCIeCntl_Destructor() PciRemoveDevice() Failed status(0x%x)\n", ret);
    }

    /* CObject_Destructor equivalent - nothing special needed in Linux */
}

/**
 * CQLCodecLib_DeletePCIe - Delete PCIe controller object
 * @d: device structure
 */
void CQLCodecLib_DeletePCIe(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodecLib_DeletePCIe()\n");

    CPCIeCntl_Destructor(d);
    /* In Windows this frees m_pPCIeCntl, but in Linux it's embedded in d */
}

/**
 * CQLCodecLib_DonePCIe - Release PCIe controller
 * @d: device structure
 */
void CQLCodecLib_DonePCIe(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodecLib_DonePCIe()\n");

    /* Disable interrupts before cleanup */
    CPCIeCntl_DisableInterrupts(&d->codec.m_hci);
}

/**
 * CQLCodecLib_DonePeripherals - Release peripheral devices
 * @d: device structure
 */
void CQLCodecLib_DonePeripherals(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodecLib_DonePeripherals()\n");

    /* Release audio codec, tuner, video encoder, etc. */
    /* For C985, most of these are not used or handled by firmware */
}

/**
 * CQLCodecLib_DeletePeripherals - Delete peripheral objects
 * @d: device structure
 */
void CQLCodecLib_DeletePeripherals(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodecLib_DeletePeripherals()\n");

    /* Nothing to delete for C985 - peripherals are on-chip */
}

/**
 * CQLCodec_Destructor - Destructor for CQLCodec
 * @d: device structure
 */
void CQLCodec_Destructor(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodec_Destructor()\n");

    /* (*(param_1->m_iMpegCodec).Release)(&param_1->m_iMpegCodec); */
    /* vtable call - we handle cleanup directly */

    if (d->codec.m_pChannelMgr != NULL) {
        CObjectMgr_Destructor(d->codec.m_pChannelMgr);
        kfree(d->codec.m_pChannelMgr);
        d->codec.m_pChannelMgr = NULL;
    }

    if (d->codec.m_pTask != NULL) {
        CTask_Destructor(d->codec.m_pTask);
        kfree(d->codec.m_pTask);
        d->codec.m_pTask = NULL;
    }

    /* QPOSMDeleteSem(m_SemaphoreFWAPI) - mutex in Linux */
    /* embedded, nothing to delete */

    /* QPOSMDeleteEvtgrp(m_EvtTask) */
    d->evt_task_flags = 0;

    /* QPOSMDeleteEvtgrp(m_EvtTaskReply) */
    d->evt_task_reply_flags = 0;

    /* QPOSMDeleteEvtgrp(m_EvtSyncDma) */
    d->evt_sync_dma_flags = 0;

    if (d->codec.m_pVideoFW != NULL) {
        if (d->codec.m_bVideoFWUpdated == 0) {
            /* QPPFMGetVideoFW(NULL, NULL) - internal ref, don't free */
        } else {
            kfree(d->codec.m_pVideoFW);
        }
    }
    d->codec.m_pVideoFW = NULL;
    d->codec.m_QL201FWSize = 0;
    d->codec.m_bVideoFWUpdated = 0;

    if (d->codec.m_pAudioFW != NULL) {
        if (d->codec.m_bAudioFWUpdated == 0) {
            /* QPPFMGetAudioFW(NULL, NULL) - internal ref, don't free */
        } else {
            kfree(d->codec.m_pAudioFW);
        }
    }
    d->codec.m_pAudioFW = NULL;
    d->codec.m_QL201AudFWSize = 0;
    d->codec.m_bAudioFWUpdated = 0;

    /* CObject_Destructor - nothing needed for embedded object */
}


/**
 * CQLCodecLib_Done - Main cleanup function
 * @d: device structure
 *
 * Returns: 0 on success
 */
int CQLCodecLib_Done(struct c985_poc *d)
{
    dev_dbg(&d->pdev->dev, "CQLCodecLib_Done()\n");

    if (d == NULL)
        return 0;

    /* Delete peripherals */
    CQLCodecLib_DeletePeripherals(d);

    /* Destructor for codec */
    CQLCodec_Destructor(d);

    /* PCIe cleanup (for PCI bus type) */
    if (d->codec.m_hci.m_bus_type == QPHCI_BUS_PCI) {
        CQLCodecLib_DonePCIe(d);
        CQLCodecLib_DeletePCIe(d);
    }

    return 0;
}

/**
 * c985_pci_remove - PCI remove callback
 * @pdev: PCI device
 *
 * Called when driver is unloaded or device removed.
 */
void c985_pci_remove(struct pci_dev *pdev)
{
    struct c985_poc *d = pci_get_drvdata(pdev);
    int leak_count;


    if (!d) {
        dev_warn(&pdev->dev, "c985_pci_remove: no device data\n");
        return;
    }

    dev_info(&pdev->dev, "Removing device...\n");

    /* 1. FIRST: Disable hardware interrupts */
    CPCIeCntl_DisableInterrupts(&d->codec.m_hci);

    /* 2. Stop encoder */
    if (d->encoder_running) {
        QPFWENCAPI_StopEncoder(d, 0, 0, 0);
        d->encoder_running = false;
        msleep(100);
    }

    /* 3. Free IRQ - no more interrupts after this */
    CPCIeCntl_UnregisterISR(d);

    /* 4. NOW cancel work - safe because no new work can be scheduled */
    cancel_work_sync(&d->irq_work);
    cancel_work_sync(&d->dma_work);

    /* 5. Unregister V4L2 */
    c985_v4l2_unregister(d);

    /* 6. Remove debugfs */
    c985_debugfs_cleanup(d);

    /* 7. Hardware cleanup (unmaps BARs, frees DMA) */
    CQLCodecLib_Done(d);

    /* 8. Check for memory leaks BEFORE freeing the device struct */
    leak_count = QPMMGetAllocCount();
    if (leak_count != 0)
        pr_warn("QPMM: Memory leak detected! %d allocations remaining.\n", leak_count);

    /* 8. PCI cleanup */
    pci_release_region(pdev, C985_BAR_MMIO);
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);

    /* 9. Clear driver data */
    pci_set_drvdata(pdev, NULL);

    /* 10. Free device structure */
    kfree(d);

    dev_info(&pdev->dev, "Device removed\n");
}
