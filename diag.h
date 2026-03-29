/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DIAG_H
#define DIAG_H

struct c985_poc;

void c985_dump_hdmi_presence(struct c985_poc *d);
void c985_dump_hdmi_mailbox(struct c985_poc *d, const char *tag);

#endif
