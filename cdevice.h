/* SPDX-License-Identifier: GPL-2.0 */
/* cdevice.h — CDevice structure for AVerMedia C985 */
#ifndef CDEVICE_H
#define CDEVICE_H

#include <linux/types.h>

#include "include/abi/qp_process_type.h"
#include "include/abi/cdevice.h"
#include "include/abi/_ksdevice.h"
#include "include/abi/_qp_task_handle.h"

/* Forward declarations */

struct _KMUTANT;
struct AVer_DependMutex;
struct _QPCODEC_USB_BUS_DATA;
struct CCrossbarProperties;
struct _CM_RESOURCE_LIST;
struct _DMA_ADAPTER;
struct ICodecLib;
struct IMpegCodec;
struct IVideoDecoder;
struct IVideoEncoder;
struct ITuner;
struct ITVAudio;
struct IAudioCodec;
struct CPinManager;
struct HAL;
struct CTaskEncode;
struct CTaskRawVideo;
struct CTaskRawAudio;
struct AVer_GPIOI2C;
struct CryptoAT88;
struct ProjectManager;
struct ProjectFactory;

struct IMpegCodec      *CDevice_getMpegCodec(struct CDevice *this);
struct project_manager *CDevice_getProjectManager(struct CDevice *this);
struct CTaskEncode     *CDevice_getEncodeHandle(struct CDevice *this);
struct CTaskRawVideo   *CDevice_getRawVidHandle(struct CDevice *this);
struct CTaskRawAudio   *CDevice_getRawAudHandle(struct CDevice *this);

/* ============================================
 * CDevice Structure
 * From Windows decompilation
 * ============================================ */


#endif /* CDEVICE_H */
