/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FIRMWARE_H
#define FIRMWARE_H

struct c985_poc;

int CQLCodec_FWDownloadAll(struct c985_poc *poc, int reset, int reload);

#endif /* FIRMWARE_H */
