/* include/abi/qp_scatter_gather_entry.h */
#ifndef _QP_SCATTER_GATHER_H
#define _QP_SCATTER_GATHER_H

#include <linux/types.h>
#include "_qp_large_integer.h"

struct _QP_SCATTER_GATHER {
    _QP_LARGE_INTEGER PhysicalAddress;  /* 0x00 */
    u32 ByteCount;                       /* 0x08 */
    u32 Alignment;                       /* 0x0C */
};                                       /* total: 0x10 */

#endif /* _QP_SCATTER_GATHER_H */
