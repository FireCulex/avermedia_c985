#pragma once

#include "_kspin.h"

typedef struct _KSPIN_DESCRIPTOR {
    ulong InterfacesCount;
    KSIDENTIFIER * Interfaces;
    ulong MediumsCount;
    KSIDENTIFIER * Mediums;
    ulong DataRangesCount;
    KSDATAFORMAT * * DataRanges;
    KSPIN_DATAFLOW DataFlow;
    KSPIN_COMMUNICATION Communication;
    _GUID * Category;
    _GUID * Name;
    union {
        long64 Reserved;
        ulong ConstrainedDataRangesCount;
    };
    KSDATAFORMAT * * ConstrainedDataRanges;
} KSPIN_DESCRIPTOR;