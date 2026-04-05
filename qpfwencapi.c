#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "avermedia_c985.h"
#include "qpfwapi.h"
#include "qpfwencapi.h"
#include "nuc100.h"
#include "cpr.h"

/*
 * QPFWCODECAPI_SystemOpen - Initialize the encoder subsystem
 * @d: device structure
 * @task_id: task ID (typically 8)
 * @function: function parameter (typically 0x80000011)
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWCODECAPI_SystemOpen(struct c985_poc *d, u32 task_id, u32 function)
{
    u32 message;
    u32 status;
    int ret;

    dev_dbg(&d->pdev->dev,
             "QPFWCODECAPI_SystemOpen: taskId=%u function=0x%08x\n",
             task_id, function);

    /* Wait for mailbox ready */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "QPFWCODECAPI_SystemOpen: mailbox not ready\n");
        return ret;
    }

    /* Write function parameter to 0x6F8 */
    writel(function, d->bar1 + REG_TO_ARM_PARAM);
    wmb();

    /* Build ARM message: (task_id << 16) | 0xF1 */
    message = (task_id << 16) | ARM_MSG_SYSTEM_OPEN;

    /* Send message with timeout=0 (we already did mailbox wait) */
    ret = QPFWAPI_SendMessageToARM(d, task_id, message, 0, 0);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "QPFWCODECAPI_SystemOpen: send failed\n");
        return ret;
    }

    /* Wait for firmware to consume the command */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "QPFWCODECAPI_SystemOpen: firmware did not consume\n");
        return ret;
    }

    /* Read response status */
    status = readl(d->bar1 + REG_FROM_ARM_MSG_STATUS);

    dev_dbg(&d->pdev->dev,
             "QPFWCODECAPI_SystemOpen: complete, response=0x%08x\n", status);

    QPFWAPI_MailboxDone(d);

    return 0;
}

int QPFWCODECAPI_SystemClose(struct c985_poc *d, u32 task_id)
{
    u32 message;
    u32 status;
    int ret;

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemClose: taskId=%u\n", task_id);

    /* Build ARM message: (task_id << 16) | 0xF3 */
    message = (task_id << 16) | ARM_MSG_SYSTEM_CLOSE;

    /* Send with timeout=500 (handles MailboxReady internally) */
    /* NOTE: No 0x6F8 write needed - SystemClose has no parameters */
    ret = QPFWAPI_SendMessageToARM(d, task_id, message, 0, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemClose: send failed\n");
        return ret;
    }

    /* Wait for firmware to consume */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemClose: firmware did not consume\n");
        return ret;
    }

    status = readl(d->bar1 + REG_FROM_ARM_MSG_STATUS);

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemClose: complete, response=0x%08x\n", status);

    return 0;
}
/*
 * QPFWCODECAPI_SystemLink - Link video/audio inputs to outputs
 * @d: device structure
 * @task_id: task ID (typically 8)
 * @video_input: video input type (4 bits)
 * @video_in_ch: video input channel (4 bits)
 * @video_output: video output type (4 bits)
 * @video_out_ch: video output channel (4 bits)
 * @audio_input: audio input type (4 bits)
 * @audio_in_ch: audio input channel (4 bits)
 * @audio_output: audio output type (4 bits)
 * @audio_out_ch: audio output channel (4 bits)
 *
 * Parameter word (0x6F8) layout:
 *   [3:0]   = video_input
 *   [7:4]   = video_in_ch
 *   [11:8]  = video_output
 *   [15:12] = video_out_ch
 *   [19:16] = audio_input
 *   [23:20] = audio_in_ch
 *   [27:24] = audio_output
 *   [31:28] = audio_out_ch
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWCODECAPI_SystemLink(struct c985_poc *d, u32 task_id,
                            u32 video_input, u32 video_in_ch,
                            u32 video_output, u32 video_out_ch,
                            u32 audio_input, u32 audio_in_ch,
                            u32 audio_output, u32 audio_out_ch)
{
    u32 param;
    u32 message;
    u32 status;
    int ret;

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemLink: taskId=%u "
             "vin=%u vin_ch=%u vout=%u vout_ch=%u "
             "ain=%u ain_ch=%u aout=%u aout_ch=%u\n",
             task_id,
             video_input, video_in_ch, video_output, video_out_ch,
             audio_input, audio_in_ch, audio_output, audio_out_ch);

    /* Wait for mailbox ready */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemLink: mailbox not ready\n");
        return ret;
    }

    /* Build parameter word - pack all 8 values into 32 bits */
    param = (video_input & 0xF)       |
    ((video_in_ch & 0xF) << 4)  |
    ((video_output & 0xF) << 8) |
    ((video_out_ch & 0xF) << 12) |
    ((audio_input & 0xF) << 16) |
    ((audio_in_ch & 0xF) << 20) |
    ((audio_output & 0xF) << 24) |
    ((audio_out_ch & 0xF) << 28);

    dev_dbg(&d->pdev->dev,
            "QPFWCODECAPI_SystemLink: param=0x%08x\n", param);

    /* Write parameter to 0x6F8 */
    writel(param, d->bar1 + REG_TO_ARM_PARAM);
    wmb();

    /* Build ARM message: (task_id << 16) | 0xF2 */
    message = (task_id << 16) | ARM_MSG_SYSTEM_LINK;

    /* Send with timeout=0 (we already did mailbox wait) */
    ret = QPFWAPI_SendMessageToARM(d, task_id, message, 0, 0);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemLink: send failed\n");
        return ret;
    }

    /* Wait for firmware to consume */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemLink: firmware did not consume\n");
        return ret;
    }

    /* Read response */
    status = readl(d->bar1 + REG_FROM_ARM_MSG_STATUS);

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemLink: complete, response=0x%08x\n", status);

    QPFWAPI_MailboxDone(d);

    return 0;
}
