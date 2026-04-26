// SPDX-License-Identifier: GPL-2.0
/*
 * qpfwencapi.c - AVerMedia C985 Encoder API
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "structs.h"
#include "avermedia_c985.h"
#include "qpfwapi.h"
#include "qpfwencapi.h"
#include "qperrors.h"

/*
 * QPFWCODECAPI_SystemOpen - Initialize the encoder subsystem
 * @codec: codec structure
 * @task_id: task ID (typically 8)
 * @function: function parameter (typically 0x80000011)
 *
 * Returns: 0 on success, negative error code on failure
 */
int QPFWCODECAPI_SystemOpen(struct cql_codec *codec, u32 task_id, u32 function)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    struct arm_message msg;
    int ret;

    /* Old chip: SystemOpen not supported */
    if ((d->codec.m_ChipType & 0xe) == 0) {
        return 0;
    }

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemOpen: taskId=%u function=0x%08x\n",
             task_id, function);

    /* Wait for mailbox ready */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "QPFWCODECAPI_SystemOpen: mailbox not ready\n");
        return ret;
    }

    /* Write function parameter to 0x6F8 */
    writel(function, c985_bar1(d) + REG_TO_ARM_PARAM0);
    wmb();

    /* Build ARM message */
    msg.Read = 0xF1;
    if ((d->codec.m_ChipType & 0xe) != 0) {
        msg.Read = (task_id << 16) | 0xF1;
    }

    /* Send message with timeout=0 (we already did mailbox wait) */
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);
    if (ret < 0) {
        dev_err(&d->pdev->dev, "QPFWCODECAPI_SystemOpen: send failed\n");
        return ret;
    }

    QPFWAPI_MailboxDone(d);
    return 0;
}

int QPFWCODECAPI_SystemClose(struct cql_codec *codec, u32 task_id)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    struct arm_message msg;
    u32 status;
    int ret;

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemClose: taskId=%u\n", task_id);

    /* Build ARM message: (task_id << 16) | 0xF3 */
    msg.Read = (task_id << 16) | ARM_MSG_SYSTEM_CLOSE;

    /* Send with timeout=500 (handles MailboxReady internally) */
    /* NOTE: No 0x6F8 write needed - SystemClose has no parameters */
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 500);
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

    status = readl(c985_bar1(d) + REG_FROM_ARM_MSG_STATUS);

    dev_info(&d->pdev->dev,
             "QPFWCODECAPI_SystemClose: complete, response=0x%08x\n", status);

    return 0;
}

/*
 * QPFWCODECAPI_SystemLink - Link video/audio inputs to outputs
 */
int QPFWCODECAPI_SystemLink(struct cql_codec *codec, u32 task_id,
                            u32 video_input, u32 video_in_ch,
                            u32 video_output, u32 video_out_ch,
                            u32 audio_input, u32 audio_in_ch,
                            u32 audio_output, u32 audio_out_ch)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    u32 param;
    struct arm_message msg;
    int ret;

    /* Power state check */
    if (codec->m_PowerState != 0) {
        dev_err(&d->pdev->dev,
                "QPFWCODECAPI_SystemLink() wrong power state(%d)\n",
                codec->m_PowerState);
        return -7;  /* Match Windows error code */
    }

    /* Old chip: SystemLink not supported */
    if ((codec->m_ChipType & 0xe) == 0) {
        return 0;
    }

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
        return ret;
    }

    /* Build parameter word */
    param = (video_input & 0xF)        |
    ((video_in_ch & 0xF) << 4)  |
    ((video_output & 0xF) << 8) |
    ((video_out_ch & 0xF) << 12) |
    ((audio_input & 0xF) << 16) |
    ((audio_in_ch & 0xF) << 20) |
    ((audio_output & 0xF) << 24) |
    ((audio_out_ch & 0xF) << 28);

    /* Write to 0x6F8 */
    writel(param, c985_bar1(d) + REG_TO_ARM_PARAM0);
    wmb();

    /* Build message */
    msg.Read = 0xF2;
    if ((codec->m_ChipType & 0xe) != 0) {
        msg.Read = (task_id << 16) | 0xF2;
    }

    /* Send */
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);

    QPFWAPI_MailboxDone(d);
    return ret;
}

/**
 * QPFWENCAPI_StartEncoder - Start the encoder
 * @d: device structure
 * @task_id: task ID (typically 8)
 */
int QPFWENCAPI_StartEncoder(struct c985_poc *d, u32 task_id)
{
    struct arm_message msg;
    int ret;

    /* Check power state */
    if (d->codec.m_PowerState != 0) {
        dev_err(&d->pdev->dev,
                "QPFWENCAPI_StartEncoder wrong power state(%d)\n",
                d->codec.m_PowerState);
        return -EINVAL;
    }

    dev_dbg(&d->pdev->dev, "QPFWENCAPI_StartEncoder() taskId(%u)\n", task_id);

    /* Build message */
    msg.Read = 1;
    if ((d->codec.m_ChipType & 0xe) != 0) {
        msg.Read = (task_id << 16) | 0x01;
    }

    /* Send message - no response expected, timeout=0 */
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);

    return ret;
}

/**
 * QPFWENCAPI_StopEncoder - Stop the encoder
 */
int QPFWENCAPI_StopEncoder(struct c985_poc *d, u32 task_id,
                           int bStopAtGOP, u32 channel_dataType)
{
    struct arm_message msg;
    u32 param;
    int ret;

    /* Check power state */
    if (d->codec.m_PowerState != 0) {
        dev_err(&d->pdev->dev,
                "QPFWENCAPI_StopEncoder wrong power state(%d)\n",
                d->codec.m_PowerState);
        return -EINVAL;
    }

    dev_dbg(&d->pdev->dev,
            "QPFWENCAPI_StopEncoder() taskId(%u) bStopAtGOP(%d) channel_dataType(%u)\n",
            task_id, bStopAtGOP, channel_dataType);

    /* Step 1: Wait for mailbox ready */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWENCAPI_StopEncoder: mailbox not ready\n");
        return ret;
    }

    /* Step 2: Write bStopAtGOP flag to 0x6F8 */
    param = (bStopAtGOP != 0) ? 1 : 0;
    writel(param, c985_bar1(d) + REG_TO_ARM_PARAM0);
    wmb();

    /* Step 3: For m_ChipType == 1 only, write to 0x6F4 */
    /* C985 has (m_ChipType & 0xe) != 0, so skip this */

    /* Step 4: Build and send message */
    msg.Read = (task_id << 16) | 0x02;

    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);

    /* Step 5: Release mailbox */
    QPFWAPI_MailboxDone(d);

    return ret;
}

/**
 * QPFWENCAPI_UpdateConfig - Update encoder configuration
 */
int QPFWENCAPI_UpdateConfig(struct c985_poc *d, u32 task_id)
{
    struct arm_message msg;

    dev_dbg(&d->pdev->dev,
            "QPFWENCAPI_UpdateConfig() taskId(%u)\n", task_id);

    /* Build message: (task_id << 16) | 0x06 */
    msg.Read = (task_id << 16) | 0x06;

    return QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);
}

/**
 * QPFWENCAPI_SetViuSyncCode - Set VIU sync codes
 */
_EQPErrors QPFWENCAPI_SetViuSyncCode(struct cql_codec *codec, u32 task_id, u32 sync_code_1, u32 sync_code_2)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    struct arm_message msg;
    _EQPErrors ret;

    /* Power state check */
    if (codec->m_PowerState != 0) {
        dev_err(&d->pdev->dev,
                "QPFWENCAPI_SetViuSyncCode wrong power state(%d)\n",
                codec->m_PowerState);
        return (_EQPErrors)-7; /* Matches decomp return value */
    }

    dev_dbg(&d->pdev->dev,
            "QPFWENCAPI_SetViuSyncCode() (0x%x) (0x%x)\n",
            sync_code_1, sync_code_2);

    /* Wait for mailbox ready */
    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0) {
        dev_err(&d->pdev->dev,
                "QPFWENCAPI_SetViuSyncCode: mailbox not ready\n");
        return ret;
    }

    /* Write sub-command selector: 2 */
    writel(2, c985_bar1(d) + 0x6F8);

    /* Write sync_code_1 */
    writel(sync_code_1, c985_bar1(d) + 0x6F4);

    /* Write sync_code_2 */
    writel(sync_code_2, c985_bar1(d) + 0x6F0);

    wmb();

    /* Build message based on ChipType */
    if ((codec->m_ChipType & 0xe) == 0) {
        /* Legacy chip: fixed command 0x2F */
        msg.Read = 0x2f;
    } else {
        /* C985 path: (task_id << 16) | 0x10 */
        msg.Read = (task_id << 16) | 0x10;
    }

    /* Send message */
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);

    /* Release mailbox */
    QPFWAPI_MailboxDone(d);

    return ret;
}

int QPFWENCAPI_SetSystemControl(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_dbg(&d->pdev->dev, "QPFWENCAPI_SetSystemControl reg (0x%04x) : (0x%08x)\n",
            d->codec.m_ENC_REG_SYSTEM_CONTROL, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_SYSTEM_CONTROL);
    return 0;
}

int QPFWENCAPI_SetPictureResolution(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_dbg(&d->pdev->dev, "QPFWENCAPI_SetPictureResolution reg (0x%04x) : (0x%08x)\n",
            d->codec.m_ENC_REG_PICTURE_RESOLUTION, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_PICTURE_RESOLUTION);
    return 0;
}

int QPFWENCAPI_SetInputControl(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetInputControl reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_INPUT_CONTROL, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_INPUT_CONTROL);
    return 0;
}

int QPFWENCAPI_SetRateControl(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetRateControl reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_RATE_CONTROL, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_RATE_CONTROL);
    return 0;
}

int QPFWENCAPI_SetVBRBitRate(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetVBRBitRate reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_BIT_RATE, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_BIT_RATE);
    return 0;
}

int QPFWENCAPI_SetFilterControl(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetFilterControl reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_FILTER_CONTROL, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_FILTER_CONTROL);
    return 0;
}

int QPFWENCAPI_SetGOPLoopFilter(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetGOPLoopFilter reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_GOP_LOOP_FILTER, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_GOP_LOOP_FILTER);
    return 0;
}

int QPFWENCAPI_SetETControl(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetETControl reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_ET_CONTROL, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_ET_CONTROL);
    return 0;
}

int QPFWENCAPI_SetBlockSize(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetBlockSize reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_BLOCK_SIZE, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_BLOCK_SIZE);
    return 0;
}

int QPFWENCAPI_SetOutPictureResolution(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetOutPictureResolution reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_OUT_PIC_RESOLUTION, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_OUT_PIC_RESOLUTION);
    return 0;
}

int QPFWENCAPI_SetAudioControlParameters(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetAudioControlParameters reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_AUDIO_CONTROL_PARAM, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_AUDIO_CONTROL_PARAM);
    return 0;
}

int QPFWENCAPI_SetAudioControlExtension(struct c985_poc *d, u32 value)
{
    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    dev_info(&d->pdev->dev, "QPFWENCAPI_SetAudioControlExtension reg (0x%04x) : (0x%08x)\n",
             d->codec.m_ENC_REG_AUDIO_CONTROL_EX, value);

    writel(value, c985_bar1(d) + d->codec.m_ENC_REG_AUDIO_CONTROL_EX);
    return 0;
}

// Add these stub implementations
_EQPErrors QPFWENCAPI_SetEncMode(struct cql_codec *codec, u32 task_id, u32 capMode, u32 trigMode, u32 gpioPin)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetEncMode task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetMJPEGQuality(struct cql_codec *codec, u32 task_id, u32 quality)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetMJPEGQuality task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetMJPEGFrameBuffer(struct cql_codec *codec, u32 task_id, u32 framebuffer)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetMJPEGFrameBuffer task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetExternalTriggerToSync(struct cql_codec *codec, u32 task_id, u32 enable, u32 gpioPin)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetExternalTriggerToSync task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetPTSResetByTrigger(struct cql_codec *codec, u32 task_id, u32 enable, u32 gpioPin, u32 immediate)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetPTSResetByTrigger task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetRawVideoDecimation(struct cql_codec *codec, u32 task_id, u32 input_fmt, u32 output_fmt, u32 scale)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetRawVideoDecimation task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetDeinterlaceMode(struct cql_codec *codec, u32 task_id, u32 mode)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetDeinterlaceMode task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetRateControlEx(struct cql_codec *codec, u32 task_id, u32 param1, u32 param2, u32 param3)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetRateControlEx task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetLowBitrateMode(struct cql_codec *codec, u32 task_id, u32 enable)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetLowBitrateMode task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetIndexCapture(struct cql_codec *codec, u32 task_id, u32 freq)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetIndexCapture task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetVBIInfo(struct cql_codec *codec, u32 task_id, u32 enable, u32 top_start, u32 top_count,
                                 u32 top_pixel, u32 top_samples, u32 bot_start, u32 bot_count,
                                 u32 bot_pixel, u32 bot_samples)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetVBIInfo task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetMP4VideoBlockNumber(struct cql_codec *codec, u32 task_id, u32 blocknum)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetMP4VideoBlockNumber task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetAudioEnhancement(struct cql_codec *codec, u32 task_id, u32 gain1, u32 gain2,
                                          u32 add, u32 sub, u32 att1, u32 att2, u32 lgain, u32 rgain)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: SetAudioEnhancement task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_EnableVidPadding(struct cql_codec *codec, u32 task_id, u32 enable)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: EnableVidPadding task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_StillVideoInput(struct cql_codec *codec, u32 task_id, u32 enable)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);
    dev_dbg(&d->pdev->dev, "STUB: StillVideoInput task=%u\n", task_id);
    return QPERR_SUCCESS;
}

_EQPErrors QPFWENCAPI_SetLargeCompressBufferControl(struct cql_codec *codec, u32 task_id, u32 value)
{
    struct c985_poc *d = container_of(codec, struct c985_poc, codec);

    struct arm_message msg;
    int ret;

    if (d->codec.m_PowerState != 0)
        return -EINVAL;

    if (d->codec.m_VerFwAPI == 0)
        return 0;

    ret = QPFWAPI_MailboxReady(d, 500);
    if (ret < 0)
        return ret;

    writel(0x14, c985_bar1(d) + 0x6F8);
    writel((value >> 31) & 1, c985_bar1(d) + 0x6F4);
    writel(value & 0x7FFFFFFF, c985_bar1(d) + 0x6F0);
    wmb();

    msg.Read = (task_id << 16) | 0x10;
    ret = QPFWAPI_SendMessageToARM(d, task_id, &msg, 0, NULL, 0);

    QPFWAPI_MailboxDone(d);
    return ret;
}
