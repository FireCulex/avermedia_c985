/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AVERIMAGETYPE_H
#define AVERIMAGETYPE_H

enum AVerImageType {
    IMAGE_HDCP          = 0,
    IMAGE_OUT_OF_RANGE  = 1,
    IMAGE_NO_SIGNAL     = 2,
    IMAGE_MAX           = 3,
    IMAGE_UNDEF         = 3,
};

#endif