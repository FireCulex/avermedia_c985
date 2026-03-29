/* SPDX-License-Identifier: GPL-2.0 */
#ifndef C985_V4L2_H
#define C985_V4L2_H

struct c985_poc;

int c985_v4l2_register(struct c985_poc *d);
void c985_v4l2_unregister(struct c985_poc *d);

#endif
