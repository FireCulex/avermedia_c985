/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KSDEVICE_H
#define _KSDEVICE_H

#include <linux/types.h>

struct _KSDEVICE_DESCRIPTOR;
struct _DEVICE_OBJECT;

enum _SYSTEM_POWER_STATE {
    PowerSystemUnspecified = 0,
    PowerSystemWorking = 1,
    PowerSystemSleeping1 = 2,
    PowerSystemSleeping2 = 3,
    PowerSystemSleeping3 = 4,
    PowerSystemHibernate = 5,
    PowerSystemShutdown = 6,
    PowerSystemMaximum = 7,
};

enum _DEVICE_POWER_STATE {
    PowerDeviceUnspecified = 0,
    PowerDeviceD0 = 1,
    PowerDeviceD1 = 2,
    PowerDeviceD2 = 3,
    PowerDeviceD3 = 4,
    PowerDeviceMaximum = 5,
};

struct _KSDEVICE {
    struct _KSDEVICE_DESCRIPTOR *Descriptor;          /* 0x00 */
    void *Bag;                                         /* 0x08 */
    void *Context;                                     /* 0x10 */
    struct _DEVICE_OBJECT *FunctionalDeviceObject;    /* 0x18 */
    struct _DEVICE_OBJECT *PhysicalDeviceObject;      /* 0x20 */
    struct _DEVICE_OBJECT *NextDeviceObject;          /* 0x28 */
    u8 Started;                                        /* 0x30 */
    u8 _padding_[2];                                   /* 0x31-0x32 */
    enum _SYSTEM_POWER_STATE SystemPowerState;        /* 0x34 */
    u8 _padding2_[2];                                 /* 0x36-0x37 */
    enum _DEVICE_POWER_STATE DevicePowerState;         /* 0x38 */
};

#endif