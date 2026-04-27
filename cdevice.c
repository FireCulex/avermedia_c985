// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "structs.h"
#include "cdevice.h"

struct IMpegCodec *CDevice_getMpegCodec(struct c_device *this)
{
    return this->m_pMpegCodec;
}

struct project_manager *CDevice_getProjectManager(struct c_device *this)
{
    //return this->m_pProjectManager;
    return 0;
}

struct CTaskEncode *CDevice_getEncodeHandle(struct c_device *this)
{
    return this->m_pTask_Encode;
}
EXPORT_SYMBOL_GPL(CDevice_getEncodeHandle);

struct CTaskRawVideo *CDevice_getRawVidHandle(struct c_device *this)
{
    return this->m_pTask_RawVid;
}
EXPORT_SYMBOL_GPL(CDevice_getRawVidHandle);

struct CTaskRawAudio *CDevice_getRawAudHandle(struct c_device *this)
{
    return this->m_pTask_RawAud;
}
EXPORT_SYMBOL_GPL(CDevice_getRawAudHandle);