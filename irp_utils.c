/* SPDX-License-Identifier: GPL-2.0 */
/*
 * irp_utils.c - IRP-related utility functions
 */

#include "avermedia_c985.h"

struct CDevice *getDevice(struct _IRP *irp)
{
    struct _KSPIN *ks_pin;
    struct _KSDEVICE *ks_dev;
    struct CDevice *device = NULL;

    ks_pin = (struct _KSPIN *)KsGetFilterFromIrp(irp);
    if (ks_pin != NULL) {
        ks_dev = KsPinGetDevice(ks_pin);
        if (ks_dev != NULL) {
            device = (struct CDevice *)ks_dev->Context;
        }
    }

    return device;
}
EXPORT_SYMBOL_GPL(getDevice);

ulong getTaskHandle(struct _IRP *irp, enum _QP_TASK_HANDLE task_type)
{
    struct CObject *filter_obj;
    u32 filter_type;
    struct CDevice *device;
    struct c_task_encode *encode_task;
    struct c_task_raw_video *vid_task;
    struct c_task_raw_audio *aud_task;
    ulong ret = 0xffffffff;
    void *ks_filter;

    ks_filter = KsGetFilterFromIrp(irp);
    filter_obj = *(struct CObject **)((char *)ks_filter + 0x10);
    device = getDevice(irp);

    if (filter_obj == NULL) {
        ret = 0xffffffff;
    }
    else {
        filter_type = CppObject_WhoAmI(filter_obj);

        if (filter_type == 0x103ea) {
            if (task_type == ENCODE_TASK_HANDLE) {
                encode_task = CDevice_getEncodeHandle(device);
                if (encode_task == NULL) {
                    ret = 0xffffffff;
                }
                else {
                    encode_task = CDevice_getEncodeHandle(device);
                    ret = CObject_IsInitialized((struct CObject *)encode_task);
                }
            }

            if (task_type == VIDEO_PASSTHROUGH_TASK_HANDLE) {
                vid_task = CDevice_getRawVidHandle(device);
                if (vid_task == NULL) {
                    ret = 0xffffffff;
                }
                else {
                    aud_task = (struct c_task_raw_audio *)CDevice_getRawVidHandle(device);
                    ret = CTaskRawAudio_getTaskHandle(aud_task);
                }
            }

            if (task_type == AUDIO_PASSTHROUGH_TASK_HANDLE) {
                aud_task = CDevice_getRawAudHandle(device);
                if (aud_task == NULL) {
                    ret = 0xffffffff;
                }
                else {
                    aud_task = CDevice_getRawAudHandle(device);
                    ret = CTaskRawAudio_getTaskHandle(aud_task);
                }
            }

            pr_debug("%s Capture Filter(0x%x) filter(0x%x) context(0x%x) hTask(%d)\n",
                     __func__, filter_obj, ks_filter, device, ret);
        }
        else if (filter_type == 0x103fc) {
            if (task_type == ENCODE_TASK_HANDLE) {
                ret = CEncoderFilter_GetEncodeTaskHandle(*(struct c_encoder_filter **)((char *)ks_filter + 0x10));
            }
        }
        else {
            ret = CDecoderFilter_GetTaskHandle(*(struct c_decoder_filter **)((char *)ks_filter + 0x10));
        }
    }

    pr_debug("%s filter(0x%x) context(0x%x) hTask(%d)\n",
             __func__, ks_filter, device, ret);

    return ret;
}
EXPORT_SYMBOL_GPL(getTaskHandle);
