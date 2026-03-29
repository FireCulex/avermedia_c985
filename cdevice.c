// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "cdevice.h"
#include "codec_lib.h"
#include "project_factory.h"
#include "project_manager.h"
#include "crossbar_properties.h"
#include "pin_manager.h"
#include "aver_mutex.h"

/*
 * CDevice_Init()
 *
 * Linux-side structural equivalent of the Windows CDevice::Init().
 * This preserves the order, dependencies, and cleanup flow,
 * but leaves all hardware-specific calls as TODOs.
 */

int CDevice_Init(struct CDevice *dev)
{
    int ret = 0;

    /* ------------------------------------------------------------ */
    /* 1. getInitData()                                             */
    /* ------------------------------------------------------------ */
    ret = getInitData(dev, &dev->m_initData);
    if (ret < 0) {
        pr_err("CDevice: getInitData() failed\n");
        return ret;
    }

    /* ------------------------------------------------------------ */
    /* 2. DMA Adapter (Windows IoGetDmaAdapter equivalent)          */
    /* ------------------------------------------------------------ */
    pr_info("CDevice: DMA init\n");

    ret = cdevice_init_dma(dev);
    if (ret < 0) {
        pr_err("CDevice: DMA init failed\n");
        return ret;
    }

    /* ------------------------------------------------------------ */
    /* 3. Codec Library Init (QPCodecInitLibrary)                   */
    /* ------------------------------------------------------------ */
    pr_info("CDevice: Codec library init\n");

    ret = CodecLib_Init(dev, &dev->m_pCodecLib);
    if (ret < 0 || !dev->m_pCodecLib) {
        pr_err("CDevice: CodecLib_Init failed\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 4. Query codec interfaces (MPEG, Decoder, Encoder, etc.)     */
    /* ------------------------------------------------------------ */
    ret = CodecLib_GetMpegCodec(dev->m_pCodecLib, &dev->m_pMpegCodec);
    if (ret < 0) goto fail_cleanup;

    ret = CodecLib_GetVideoDecoder(dev->m_pCodecLib, &dev->m_pVidDecoder);
    if (ret < 0) goto fail_cleanup;

    ret = CodecLib_GetVideoEncoder(dev->m_pCodecLib, &dev->m_pVidEncoder);
    if (ret < 0) goto fail_cleanup;

    ret = CodecLib_GetTuner(dev->m_pCodecLib, &dev->m_pTuner);
    if (ret < 0) goto fail_cleanup;

    ret = CodecLib_GetTVAudio(dev->m_pCodecLib, &dev->m_pTVAudio);
    if (ret < 0) goto fail_cleanup;

    ret = CodecLib_GetAudioCodec(dev->m_pCodecLib, &dev->m_pAudCodec);
    if (ret < 0) goto fail_cleanup;

    /* ------------------------------------------------------------ */
    /* 5. InitDevice()                                              */
    /* ------------------------------------------------------------ */
    ret = CodecLib_InitDevice(dev->m_pCodecLib, dev);
    if (ret < 0) {
        pr_err("CDevice: InitDevice failed\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 6. Free firmware blobs (Windows operator_delete[])           */
    /* ------------------------------------------------------------ */
    if (dev->m_initData.mpgCodecInitData.pVideoFW) {
        kfree(dev->m_initData.mpgCodecInitData.pVideoFW);
        dev->m_initData.mpgCodecInitData.pVideoFW = NULL;
    }

    if (dev->m_initData.mpgCodecInitData.pAudioFW) {
        kfree(dev->m_initData.mpgCodecInitData.pAudioFW);
        dev->m_initData.mpgCodecInitData.pAudioFW = NULL;
    }

    /* ------------------------------------------------------------ */
    /* 7. Allocate raw output tasks                                 */
    /* ------------------------------------------------------------ */
    ret = allocateRawVideoOutputTask(dev);
    if (ret < 0) goto fail_cleanup;

    ret = allocateRawAudioOutputTask(dev);
    if (ret < 0) goto fail_cleanup;

    /* ------------------------------------------------------------ */
    /* 8. USB vs PCI security EEPROM check                          */
    /* ------------------------------------------------------------ */
    if (dev->m_initData.mpgCodecInitData.BusType == BUS_USB) {
        detectFactoryDriver(dev);
    } else {
        ret = checkCryptoAT88(dev);
        if (ret < 0) {
            pr_err("CDevice: Security EEPROM check failed\n");
            goto fail_cleanup;
        }
    }

    /* ------------------------------------------------------------ */
    /* 9. ProjectFactory                                            */
    /* ------------------------------------------------------------ */
    dev->m_pProjectFactory = ProjectFactory_Create(dev);
    if (!dev->m_pProjectFactory) {
        pr_err("CDevice: Failed to allocate ProjectFactory\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 10. HAL                                                      */
    /* ------------------------------------------------------------ */
    dev->m_phal = HAL_Create(dev->m_pCodecLib);
    if (!dev->m_phal) {
        pr_err("CDevice: Failed to allocate HAL\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 11. ProjectManager                                           */
    /* ------------------------------------------------------------ */
    dev->m_pProjectManager =
    ProjectFactory_CreateProject(dev->m_pProjectFactory,
                                 dev,
                                 dev->m_pCodecLib,
                                 dev->m_pMpegCodec);

    if (!dev->m_pProjectManager) {
        pr_err("CDevice: Failed to allocate ProjectManager\n");
        goto fail_cleanup;
    }

    ret = ProjectManager_Init(dev->m_pProjectManager);
    if (ret < 0) goto fail_cleanup;

    /* ------------------------------------------------------------ */
    /* 12. Filter factories                                         */
    /* ------------------------------------------------------------ */
    ret = createFilterFactories(dev);
    if (ret < 0) goto fail_cleanup;

    /* ------------------------------------------------------------ */
    /* 13. Crossbar + PinManager                                    */
    /* ------------------------------------------------------------ */
    dev->m_p_crossbar_properties = CrossbarProperties_Allocate(dev);

    dev->m_pPinsMgr = PinManager_Create(dev, 2, 1);
    if (!dev->m_pPinsMgr) {
        pr_err("CDevice: Failed to create PinManager\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 14. Device mutex                                             */
    /* ------------------------------------------------------------ */
    dev->m_pDeviceMutex = AVerMutex_Create();
    if (!dev->m_pDeviceMutex) {
        pr_err("CDevice: Failed to create device mutex\n");
        goto fail_cleanup;
    }

    /* ------------------------------------------------------------ */
    /* 15. Encoder-only mode                                        */
    /* ------------------------------------------------------------ */
    if (dev->m_dwEncoderOnly == 1) {
        releaseRawAudioOutputTask(dev);
        releaseRawVideoOutputTask(dev);
        allocateEncodeTask(dev);
        setCurrentTask(dev, 0);
        CTaskEncode_loadSettings(getEncodeHandle(dev));
    }

    /* ------------------------------------------------------------ */
    /* 16. Crossbar default routing                                 */
    /* ------------------------------------------------------------ */
    ProjectManager_SetVideoInput(dev->m_pProjectManager, 6);
    msleep(30);

    ProjectManager_SetAudioInput(dev->m_pProjectManager, 3);
    msleep(30);

    /* ------------------------------------------------------------ */
    /* 17. Final CppObject Init                                     */
    /* ------------------------------------------------------------ */
    ret = CppObject_Init((struct CppObject *)dev);
    if (ret < 0) goto fail_cleanup;

    return 0;

    fail_cleanup:
    pr_err("CDevice_Init: cleanup path triggered\n");
    CDevice_Cleanup(dev);
    return ret;
}
