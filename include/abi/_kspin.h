/* include/abi/_kspin.h */
#ifndef _KSPIN_H
#define _KSPIN_H

#include <linux/types.h>

typedef enum _KSPIN_COMMUNICATION {
    KSPIN_COMMUNICATION_NONE = 0,
    KSPIN_COMMUNICATION_SINK = 1,
    KSPIN_COMMUNICATION_SOURCE = 2,
    KSPIN_COMMUNICATION_BOTH = 3,
    KSPIN_COMMUNICATION_BRIDGE = 4
} KSPIN_COMMUNICATION;

typedef enum _KSPIN_DATAFLOW {
    KSPIN_DATAFLOW_IN = 0,
    KSPIN_DATAFLOW_OUT = 1
} KSPIN_DATAFLOW;

typedef enum _KSSTATE {
    KSSTATE_STOP = 0,
    KSSTATE_ACQUIRE = 1,
    KSSTATE_PAUSE = 2,
    KSSTATE_RUN = 3
} KSSTATE;

typedef enum _KSRESET {
    KSRESET_ENDOFSTREAM = 0,
    KSRESET_RUNNING = 1
} KSRESET;

struct KSIDENTIFIER {
    u8 _data[24];               /* 0x00 - union placeholder */
};                              /* total: 0x18 */

struct KSPRIORITY {
    u32 PriorityClass;          /* 0x00 */
    u32 PrioritySubClass;       /* 0x04 */
};                              /* total: 0x08 */

struct KSDATAFORMAT;
struct KSMULTIPLE_ITEM;
struct _KSPIN_DISPATCH;
struct KSAUTOMATION_TABLE_;
struct KSALLOCATOR_FRAMING_EX;
struct _GUID;

struct KSPIN_DESCRIPTOR {
    u32 InterfacesCount;                        /* 0x00 */
    u32 _pad04;                                 /* 0x04 */
    struct KSIDENTIFIER *Interfaces;            /* 0x08 */
    u32 MediumsCount;                           /* 0x10 */
    u32 _pad14;                                 /* 0x14 */
    struct KSIDENTIFIER *Mediums;               /* 0x18 */
    u32 DataRangesCount;                        /* 0x20 */
    u32 _pad24;                                 /* 0x24 */
    struct KSDATAFORMAT **DataRanges;           /* 0x28 */
    KSPIN_DATAFLOW DataFlow;                    /* 0x30 */
    KSPIN_COMMUNICATION Communication;          /* 0x34 */
    struct _GUID *Category;                     /* 0x38 */
    struct _GUID *Name;                         /* 0x40 */
    u64 _reserved48;                            /* 0x48 */
    struct KSDATAFORMAT **ConstrainedDataRanges; /* 0x50 */
};                                              /* total: 0x58 */

struct _KSPIN_DESCRIPTOR_EX {
    struct _KSPIN_DISPATCH *Dispatch;           /* 0x00 */
    struct KSAUTOMATION_TABLE_ *AutomationTable; /* 0x08 */
    struct KSPIN_DESCRIPTOR PinDescriptor;      /* 0x10 */
    u32 Flags;                                  /* 0x68 */
    u32 InstancesPossible;                      /* 0x6C */
    u32 InstancesNecessary;                     /* 0x70 */
    u32 _pad74;                                 /* 0x74 */
    struct KSALLOCATOR_FRAMING_EX *AllocatorFraming; /* 0x78 */
    void *IntersectHandler;                     /* 0x80 */
};                                              /* total: 0x88 */

struct _KSPIN {
    struct _KSPIN_DESCRIPTOR_EX *Descriptor;    /* 0x00 */
    void *Bag;                                  /* 0x08 */
    void *Context;                              /* 0x10 */
    u32 Id;                                     /* 0x18 */
    KSPIN_COMMUNICATION Communication;          /* 0x1C */
    u8 ConnectionIsExternal;                    /* 0x20 */
    u8 _pad21[7];                               /* 0x21 */
    struct KSIDENTIFIER ConnectionInterface;    /* 0x28 */
    struct KSIDENTIFIER ConnectionMedium;       /* 0x40 */
    struct KSPRIORITY ConnectionPriority;       /* 0x58 */
    struct KSDATAFORMAT *ConnectionFormat;      /* 0x60 */
    struct KSMULTIPLE_ITEM *AttributeList;      /* 0x68 */
    u32 StreamHeaderSize;                       /* 0x70 */
    KSPIN_DATAFLOW DataFlow;                    /* 0x74 */
    KSSTATE DeviceState;                        /* 0x78 */
    KSRESET ResetState;                         /* 0x7C */
    KSSTATE ClientState;                        /* 0x80 */
};                                              /* total: 0x84 */

typedef struct _KSPIN KSPIN;
typedef struct _KSPIN *PKSPIN;

#endif /* _KSPIN_H */
