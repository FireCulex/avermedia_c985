// SPDX-License-Identifier: GPL-2.0
// diag.c — Diagnostic dump functions for AVerMedia C985

#include <linux/io.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "diag.h"

void c985_dump_hdmi_presence(struct c985_poc *d)
{
    u8 v = readb(d->bar1 + 0x310);
    dev_info(&d->pdev->dev, "HDMI presence = 0x%02x (bit0=%d)\n", v, v & 1);
}

void c985_dump_hdmi_mailbox(struct c985_poc *d, const char *tag)
{
    int i;
    u8 buf[0x40];

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = readb(d->bar1 + 0x300 + i);

    dev_info(&d->pdev->dev, "%s: HDMI mailbox (0x300..0x33F):\n", tag);
    for (i = 0; i < sizeof(buf); i += 16) {
        dev_info(&d->pdev->dev,
                 "  +0x%02x: %02x %02x %02x %02x  %02x %02x %02x %02x  "
                 "%02x %02x %02x %02x  %02x %02x %02x %02x\n",
                 i,
                 buf[i+0], buf[i+1], buf[i+2], buf[i+3],
                 buf[i+4], buf[i+5], buf[i+6], buf[i+7],
                 buf[i+8], buf[i+9], buf[i+10], buf[i+11],
                 buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
    }
}
