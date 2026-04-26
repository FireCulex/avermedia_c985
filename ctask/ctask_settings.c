// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_settings.c - Load default settings and reset state
 */

#include "ctask_private.h"

/* ================================================================
 * CTask_LoadDefaultSettings
 * ================================================================ */
void CTask_LoadDefaultSettings(struct c_task *task, struct task_data *td)
{
    struct cql_codec *codec = task->m_pMpegCodec;

    /* Modern chip path (C985, m_ChipType=8) */
    if ((codec->m_ChipType & 0x0E) != 0) {
        /* System Control */
        td->m_systemControl = 0;
        td->m_systemControl = (td->m_systemControl & 0xFFFFFFF8) | 3;
        td->m_systemControl = (td->m_systemControl & 0xFFFFFF07) | 8;
        td->m_systemControl = (td->m_systemControl & 0xFFFFF0FF) | 0x200;
        td->m_systemControl = (td->m_systemControl & 0xFFFF0FFF) | 0xB000;
        td->m_systemControl &= 0xFF00FFFF;
        td->m_systemControl = (td->m_systemControl & 0xFCFFFFFF) | 0x1000000;
        td->m_systemControl &= 0xFBFFFFFF;
        td->m_systemControl &= 0xE7FFFFFF;
        td->m_systemControl = (td->m_systemControl & 0x9FFFFFFF) | 0x20000000;
        if (codec->m_VerFwAPI == 1)
            td->m_systemControl &= 0x7FFFFFFF;

        /* Picture Resolution - 1280x720 default */
        td->m_pictureResolution = 0;
        td->m_pictureResolution = (td->m_pictureResolution & 0xFFFFF800) | 0x500;
        td->m_pictureResolution = (td->m_pictureResolution & 0xF800FFFF) | 0x2D00000;

        /* Sync Mode */
        td->m_syncMode = 5;

        /* Input Control */
        td->m_inputControl = 0;
        td->m_inputControl = (td->m_inputControl & 0xFFFFFFF8) | 1;
        td->m_inputControl = (td->m_inputControl & 0xFFFFFFC7) | 0x10;
        td->m_inputControl = (td->m_inputControl & 0xFFFFFE7F) |
        ((g_syncModeTable[td->m_syncMode] & 3) << 7);
        td->m_inputControl = (td->m_inputControl & 0xFFFF01FF) | 0x600;
        td->m_inputControl = (td->m_inputControl & 0xFF80FFFF) | 0x5E0000;
        td->m_inputControl = (td->m_inputControl & 0xE07FFFFF) | 0xF000000;
        td->m_inputControl &= 0xDFFFFFFF;
        td->m_inputControl &= 0xBFFFFFFF;
        td->m_inputControl = (td->m_inputControl & 0x7FFFFFFF) |
        (codec->m_ClkEdge << 0x1F);

        /* Rate Control */
        td->m_rateControl = 0;
        td->m_rateControl = (td->m_rateControl & 0xFFFF0000) | 4000;
        td->m_rateControl = (td->m_rateControl & 0xF000FFFF) | 0x500000;
        td->m_rateControl = (td->m_rateControl & 0xCFFFFFFF) | 0x10000000;
        td->m_rateControl &= 0xBFFFFFFF;
        td->m_rateControl |= 0x80000000;

        /* Rate Control Ex */
        td->m_rateControlEx = (td->m_rateControlEx & 0xFFFFF000) | 0x50;
        td->m_rateControlEx &= 0xFFFFEFFF;
        td->m_rateControlEx = (td->m_rateControlEx & 0xE0001FFF) | 0x14000;

        /* Bit Rate */
        td->m_bitRate = 0;
        td->m_bitRate = (td->m_bitRate & 0xFFFF0000) | 2000;
        td->m_bitRate = (td->m_bitRate & 0x0000FFFF) | 0x1F400000;

        /* Filter Control */
        td->m_filterControl = 0;
        td->m_filterControl = (td->m_filterControl & 0xFFFFF800) |
        (codec->m_VIUStartPixel & 0x7FF);
        td->m_filterControl = (td->m_filterControl & 0xF800FFFF) |
        ((codec->m_VIUStartLine & 0x7FF) << 0x10);
        td->m_filterControl &= 0xFFFFF7FF;
        td->m_filterControl = (td->m_filterControl & 0xFFFF0FFF) | 0x2000;

        /* GOP Loop Filter */
        td->m_gopLoopFilter = 0;
        td->m_gopLoopFilter = (td->m_gopLoopFilter & 0xFFFF0000) | 0x1E;
        td->m_gopLoopFilter |= 0x10000;
        td->m_gopLoopFilter &= 0xFFFDFFFF;
        td->m_gopLoopFilter &= 0xFFC3FFFF;
        td->m_gopLoopFilter &= 0xFC3FFFFF;
        td->m_gopLoopFilter = (td->m_gopLoopFilter & 0xF3FFFFFF) | 0x4000000;
        td->m_gopLoopFilter &= 0xEFFFFFFF;
        td->m_gopLoopFilter |= 0x20000000;
        td->m_gopLoopFilter |= 0x40000000;
        td->m_gopLoopFilter &= 0x7FFFFFFF;

        /* Block Transfer Size */
        td->m_blkXferSize = 0;
        td->m_blkXferSize = (td->m_blkXferSize & 0xFFFF0000) | 0x10;

        /* Output Picture Resolution */
        td->m_outPictureResolution = 0;
        td->m_outPictureResolution = (td->m_outPictureResolution & 0xFFFFF800) | 0x500;
        td->m_outPictureResolution = (td->m_outPictureResolution & 0xF800FFFF) | 0x2D00000;

        /* Audio Control Param */
        td->m_audioControlParam = 0;
        td->m_audioControlParam = (td->m_audioControlParam & 0xFFFFFC00) | 0x80;
        td->m_audioControlParam &= 0xFFFFF3FF;
        td->m_audioControlParam = (td->m_audioControlParam & 0xFFFF0FFF) | 0x1000;
        td->m_audioControlParam = (td->m_audioControlParam & 0xFE7FFFFF) | 0x1000000;
        td->m_audioControlParam &= 0xFDFFFFFF;
        td->m_audioControlParam &= 0xFBFFFFFF;
        td->m_audioControlParam &= 0xF7FFFFFF;
        td->m_audioControlParam = (td->m_audioControlParam & 0x0FFFFFFF) | 0x20000000;

        /* Audio Control Ex */
        td->m_audioControlExAAC = 0;
        td->m_audioControlExAAC = (td->m_audioControlExAAC & 0xFFFFFFF8) | 2;
        td->m_audioControlExAAC &= 0xFFFFFFF7;
        td->m_audioControlExAAC |= 0x10;
        td->m_audioControlExAAC |= 0x20;
        td->m_audioControlExAAC |= 0x40;
        td->m_audioControlExAAC |= 0x80;
        td->m_audioControlExAAC = (td->m_audioControlExAAC & 0x0000FFFF) | 0x46500000;
        td->m_audioControlExAAC &= 0xFFFF7FFF;

        td->m_audioControlExG711 = 0;
        td->m_audioControlExG711 &= 0xFFFFFFFE;
        td->m_audioControlExG711 &= 0xFFFF7FFF;
        td->m_audioControlExG711 &= 0xFFFFFFF9;

        td->m_audioControlExLPCM = 0;
        td->m_audioControlExLPCM = (td->m_audioControlExLPCM & 0xFFFFF800) | 0x200;
        td->m_audioControlExLPCM &= 0xFFFF7FFF;

        td->m_audioControlExSILK = 0;
        td->m_audioControlExSILK = (td->m_audioControlExSILK & 0xFFFFFFFC) | 2;
        td->m_audioControlExSILK &= 0xFFFF7FFF;

        /* Misc encoder settings */
        td->m_EncIndexCapFreq = 0x20;
        td->m_MP4VideoBlockNumber = 5;
        td->m_EncStopMode = 0;
        td->m_bEncEnableVidPadding = 1;
        td->m_bEncVidFrozen = 0;
        td->m_bEncVidStillInput = 0;
        td->m_EncMjpegQuality = 0;
        td->m_EncMjpegFrameBuffer = 0;
        td->m_DeinterlaceMode = 1;
        td->m_LargeCompressBufferControl = 12000;
        td->m_EnableLowBitrateMode = 0;

        /* Encoder addresses */
        td->m_encFontTableAddr = 0xFFFFFFFF;
        td->m_encTextListAddr = 0xFFFFFFFF;
        td->m_encVidBufLumaAddr = 0xFFFFFFFF;
        td->m_encVidBufChromaAddr = 0xFFFFFFFF;
        td->m_encFrameLABufAddr = 0xFFFFFFFF;
        td->m_encTopLABufAddr = 0xFFFFFFFF;
        td->m_encBottomLABufAddr = 0xFFFFFFFF;
        td->m_encVidBufPTS = 0;

        /* Stream ID */
        if (codec->m_VerFwAPI == 1) {
            td->m_streamID = 0;
            td->m_streamID = (td->m_streamID & 0xFFFFFFF0) | 3;
            td->m_streamID = (td->m_streamID & 0xFFFFFF0F) | 0x10;
            td->m_streamID |= 0x80000000;
        } else {
            td->m_streamID = 4;
        }

        td->m_outputMode = 1;

        /* Decode Resolution */
        td->m_decodeResolution = 0;
        td->m_decodeResolution = (td->m_decodeResolution & 0xFFFFF800) | 0x500;
        td->m_decodeResolution = (td->m_decodeResolution & 0xF800FFFF) | 0x2D00000;

        /* Decoder settings */
        td->m_decStopMode = 0;
        td->m_decStopDispMode = 0;
        td->m_decPauseDispMode = 0;
        td->m_decTrickPlaySpeed = 0x101;
        td->m_decTrickPlayDirection = 0;
        td->m_decTrickFieldPreference = 0;
        td->m_decDispMode = 3;
        td->m_decDispTVSystem = 3;
        td->m_decXferMode = 1;
        td->m_decXferMinimumSize = 0;
        td->m_decFWSharedMemWr = 0xFFFFFFFF;
        td->m_decFWSharedMemRd = 0xFFFFFFFF;

        /* Codec function */
        if (codec->m_VerFwAPI == 0) {
            td->m_codec_function = (td->type == TASK_TYPE_ENC) ? 1 : 2;
        } else {
            td->m_codec_function = 0;
            td->m_codec_function = (td->m_codec_function & 0xFFFFFFF0) | 1;
            td->m_codec_function = (td->m_codec_function & 0xFFFFFF0F) | 0x10;
            td->m_codec_function = (td->m_codec_function & 0xFFFFFEFF) |
            ((td->type != TASK_TYPE_ENC) << 8);
            td->m_codec_function |= 0x80000000;
        }

        /* Video/Audio routing */
        td->m_video_input = 1;
        td->m_video_in_ch = 0;
        td->m_video_output = 1;
        td->m_video_out_ch = 0;
        td->m_audio_input = (td->type != TASK_TYPE_ENC) ? 1 : 0;
        td->m_audio_in_ch = 0;
        td->m_audio_output = (td->type == TASK_TYPE_ENC) ? 1 : 0;
        td->m_audio_out_ch = 0;
    }
    /* TODO: Legacy chip path if needed */
}

/* ================================================================
 * CTask_ResetState
 * ================================================================ */
void CTask_ResetState(struct c_task *task, struct task_data *td)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    enum task_data_type dtype;
    int ret;

    dev_dbg(&poc->pdev->dev, "CTask_ResetState() (%d)\n", td->id);

    for (dtype = TASK_DATA_TYPE_COMP_VID; (int)dtype < TASK_DATA_TYPE_MAX; dtype++) {
        /* Drain ARM message FIFO */
        if (td->pArmMsgFifo[(int)dtype]) {
            do {
                if (task->CompleteArm)
                    ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                    task->CompleteArm)(task, td, dtype);
                ret = CFifo_GetFifo(td->pArmMsgFifo[(int)dtype],
                                    &td->ArmRequest[(int)dtype]);
            } while (ret != 0);
        }

        memset(&td->UserBuffer[(int)dtype], 0, sizeof(struct task_user_buffer));
        memset(&td->ArmRequest[(int)dtype], 0, sizeof(struct task_arm_request));

        td->ArmRequestNumber[(int)dtype] = 0;
        td->pBufDescToCancel[(int)dtype] = NULL;
        td->bFlushing[(int)dtype] = 0;
    }

    td->ArmBufferAddr = 0;
    td->m_dwStarted = 0;
    td->m_dwPaused = 0;
    td->m_bAcquired = 0;
    td->m_State = TASK_STATE_IDLE;
    td->m_Error = 0;
    td->m_StartID = 0;
    td->bDone = 0;
}
