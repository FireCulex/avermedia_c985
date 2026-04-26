// SPDX-License-Identifier: GPL-2.0
/*
 * ctask_arm.c - ARM message and completion handling
 */

#include "ctask_private.h"

/* ================================================================
 * CTask_CompleteArm
 * ================================================================ */
void CTask_CompleteArm(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct host_message reply;
    struct arm_message arm_reply;
    u32 size, pts, pts_valid, last, frame_flags, fw_data;
    u32 data_type_fw;
    u32 orig_cmd;
    int ret;
    struct host_message_status status;
    int idx = (int)data_type;

    dev_info(&poc->pdev->dev, "DEBUG: dumping ArmRequest[%d] raw bytes:", idx);
    print_hex_dump(KERN_INFO, "  ", DUMP_PREFIX_OFFSET, 16, 1,
                   &td->ArmRequest[idx], 72, true);

    dev_dbg(&poc->pdev->dev,
            "CTask_CompleteArm() cmd(0x%x) status(0x%x) reqid(%d)\n",
            td->ArmRequest[idx].HostMsg.Read,
            td->ArmRequest[idx].HostMsg_status.Read,
            idx);

    /* The valid field in arm_buffer_all doesn't mean "is this entry valid" - it maps to ulYAddr in the YUVRAS variant.
     * The union is working correctly - it's just that ALL.valid was never meant to be used as a boolean validity check for YUVRAS buffers.*/
    if (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.valid == 0)
        return;

    /* Extract common fields based on buffer type */
    data_type_fw = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.ulDataType;
    pts = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.ulPTS;
    pts_valid = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.ulPTSValid;
    last = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved7;
    frame_flags = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved8;
    fw_data = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.ulFwData;

    switch (td->ArmRequest[idx].ArmBuffer.type) {
        case ARM_BUF_OTHERS:
            size = td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 >> 2;
            break;
        case ARM_BUF_YUV:
    size = (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 +
            td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved5) >> 2;
            break;
        case ARM_BUF_YUVMB2RAS:
        case ARM_BUF_YUVRAS:
            size = (td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved4 +
            td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved5 +
            td->ArmRequest[idx].ArmBuffer.BUFFER.ALL.reserved6) >> 2;
            break;
        default:
            size = 0;
            break;
    }

    orig_cmd = td->ArmRequest[idx].HostMsg.Read;
    status = td->ArmRequest[idx].HostMsg_status;

    /* Clear ARM request */
    memset(&td->ArmRequest[idx].ArmBuffer.BUFFER, 0, 0x48);

    ret = QPFWAPI_MailboxReady(poc, 1000);
    if (ret < 0) {
        dev_err(&poc->pdev->dev, "CTask_CompleteArm() Mailbox BUSY!!!!\n");
        return;
    }

    reply.Read = orig_cmd & 0xFFFF0000;
    orig_cmd &= 0xFFFF;

    if (orig_cmd < 0x40) {
        dev_err(&poc->pdev->dev,
                "CTask_CompleteArm() Unknown toHostCmd(0x%x)!!!\n", orig_cmd);
        QPFWAPI_MailboxDone(poc);
        return;
    }

    if (orig_cmd < 0x42) {
        reply.Read |= 0x30;

        /* Write registers */
        if (task->m_pMpegCodec->m_hci.RegisterWrite) {
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F8, data_type_fw);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F4, size);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F0, pts);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6EC, pts_valid);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6E4, fw_data);
        }
    } else if (orig_cmd == 0xC0) {
        u32 reply_cmd = last ? 0xA1 : 0xA0;
        reply.Read |= reply_cmd;

        if (task->m_pMpegCodec->m_hci.RegisterWrite) {
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F8, data_type_fw);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F4, size);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6F0, pts);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6EC, pts_valid);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6E8, frame_flags);
            ((int (*)(struct ihciapi *, u16, u32))
            task->m_pMpegCodec->m_hci.RegisterWrite)(&task->m_pMpegCodec->m_hci, 0x6E4, fw_data);
        }
    } else {
        dev_err(&poc->pdev->dev,
                "CTask_CompleteArm() Unknown toHostCmd(0x%x)!!!\n", orig_cmd);
        QPFWAPI_MailboxDone(poc);
        return;
    }

    dev_dbg(&poc->pdev->dev,
            "CTask_CompleteArm() toArmCmd(0x%x) channel(%d) type(0x%x) size(0x%x)\n",
            reply.Read & 0xFFFF, td->id, data_type_fw, size);

    arm_reply.Read = reply.Read;

    QPFWAPI_SendMessageToARM(poc, td->id, &arm_reply, 0, &status, 0);
    QPFWAPI_MailboxDone(poc);
}

/* ================================================================
 * CTask_ProcessArmMessage
 * ================================================================ */
void CTask_ProcessArmMessage(struct c_task *task)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct host_message msg;
    struct host_message_status status[3];
    u32 p0, p1, p2, p3, p4;
    int ret;

    dev_dbg(&poc->pdev->dev, "CTask_ProcessArmMessage()\n");

    ret = QPFWAPI_GetARMMessage(poc, &msg, status, &p0, &p1, &p2, &p3, &p4);
    if (ret < 0) {
        dev_info(&poc->pdev->dev,
                 "CTask_ProcessArmMessage() WaitForARMMessage() Failed status(%d)!!\n", ret);
        return;
    }

    if ((msg.Read & 0xFFFF) < 0x80) {
        u32 cmd = msg.Read & 0xFFFF;

        if (cmd == 0x40) {
            /*
             * p0 encodes task_slot in upper 16 bits, data_type in lower 16.
             * This is a common firmware convention - adjust if raw dumps
             * show a different encoding.
             */
            u32 task_slot    = (p0 >> 16) & 0x7;
            u32 data_type    = p0 & 0xFFFF;

            /* -------------------------------------------------- *
             * PRE-ENQUEUE DUMP                                    *
             * -------------------------------------------------- */
            dev_info(&poc->pdev->dev,
                     "=== CMD 0x40 ARM MESSAGE (pre-enqueue) ===\n"
                     "  msg.Read       : 0x%08x  cmd=0x%02x  chan=0x%04x\n"
                     "  status[0].Read : 0x%08x  (status=%u rply=%u msg_id=0x%04x)\n"
                     "  status[1].Read : 0x%08x  (status=%u rply=%u msg_id=0x%04x)\n"
                     "  status[2].Read : 0x%08x  (status=%u rply=%u msg_id=0x%04x)\n"
                     "  p0=0x%08x  p1=0x%08x  p2=0x%08x  p3=0x%08x  p4=0x%08x\n"
                     "  decoded: task_slot=%u  data_type=%u (%s)\n",
                     /* msg */
                     msg.Read, msg.Reg.command, msg.Reg.reserved,
                     /* status[0] */
                     status[0].Read,
                     status[0].Reg.status, status[0].Reg.rply, status[0].Reg.msg_id,
                     /* status[1] */
                     status[1].Read,
                     status[1].Reg.status, status[1].Reg.rply, status[1].Reg.msg_id,
                     /* status[2] */
                     status[2].Read,
                     status[2].Reg.status, status[2].Reg.rply, status[2].Reg.msg_id,
                     /* params */
                     p0, p1, p2, p3, p4,
                     /* decoded */
                     task_slot, data_type,
                     data_type == TASK_DATA_TYPE_COMP_VID ? "COMP_VID" :
                     data_type == TASK_DATA_TYPE_COMP_AUD ? "COMP_AUD" :
                     data_type == TASK_DATA_TYPE_INDEX    ? "INDEX"    :
                     data_type == TASK_DATA_TYPE_VBI      ? "VBI"      :
                     data_type == TASK_DATA_TYPE_YUV      ? "YUV"      :
                     data_type == TASK_DATA_TYPE_PCM      ? "PCM"      :
                     data_type == TASK_DATA_TYPE_VIRTUAL  ? "VIRTUAL"  : "UNKNOWN");

            /* Dump task_data context if slot is valid */
            if (task_slot < 8) {
                struct task_data *td = &task->m_TaskData[task_slot];

                dev_info(&poc->pdev->dev,
                         "  task_data[%u]: id=%u type=%u valid=%d state=%u\n"
                         "  FWDataType[0..6]: %u %u %u %u %u %u %u\n"
                         "  pArmMsgFifo[0..6]: %px %px %px %px %px %px %px\n",
                         task_slot, td->id, td->type, td->valid, td->m_State,
                         td->FWDataType[0], td->FWDataType[1], td->FWDataType[2],
                         td->FWDataType[3], td->FWDataType[4], td->FWDataType[5],
                         td->FWDataType[6],
                         td->pArmMsgFifo[0], td->pArmMsgFifo[1], td->pArmMsgFifo[2],
                         td->pArmMsgFifo[3], td->pArmMsgFifo[4], td->pArmMsgFifo[5],
                         td->pArmMsgFifo[6]);

                /* Dump the FIFO for this data_type if pointer is valid */
                if (data_type < 7 && td->pArmMsgFifo[data_type]) {
                    struct c_fifo *fifo =
                    (struct c_fifo *)td->pArmMsgFifo[data_type];

                    dev_info(&poc->pdev->dev,
                             "  FIFO[%u] @ %px: RdPtr=%u WrPtr=%u "
                             "Level=%u Size=%u EntrySize=%u Buf=%px\n",
                             data_type, fifo,
                             fifo->m_dwReadPtr, fifo->m_dwWritePtr,
                             fifo->m_dwFifoLevel, fifo->m_size,
                             fifo->m_sizeEntry, fifo->m_Fifo);

                    /* Snapshot the write slot BEFORE enqueue */
                    if (fifo->m_Fifo && fifo->m_sizeEntry > 0 &&
                        fifo->m_dwWritePtr < fifo->m_size) {
                        u32 off = fifo->m_dwWritePtr * fifo->m_sizeEntry;
                    dev_info(&poc->pdev->dev,
                             "  PRE  write-slot[%u] raw bytes:\n",
                             fifo->m_dwWritePtr);
                    print_hex_dump(KERN_INFO,
                                   "    PRE:  ", DUMP_PREFIX_OFFSET,
                                   16, 4,
                                   fifo->m_Fifo + off,
                                   min_t(u32, fifo->m_sizeEntry, 128),
                                   false);
                        }
                }
            }
        } /* end if cmd == 0x40 (pre) */

        /* -------------------------------------------------- *
         * CALL THE REAL FUNCTION                             *
         * -------------------------------------------------- */
        CEncoderTask_ProcessArmMessage(task, &msg, status, p0, p1, p2, p3, p4);

        /* -------------------------------------------------- *
         * POST-ENQUEUE DUMP                                  *
         * -------------------------------------------------- */
        if (cmd == 0x40) {
            u32 task_slot = (p0 >> 16) & 0x7;
            u32 data_type = p0 & 0xFFFF;

            if (task_slot < 8 && data_type < 7) {
                struct task_data *td = &task->m_TaskData[task_slot];

                if (td->pArmMsgFifo[data_type]) {
                    struct c_fifo *fifo =
                    (struct c_fifo *)td->pArmMsgFifo[data_type];

                    dev_info(&poc->pdev->dev,
                             "  POST FIFO[%u]: RdPtr=%u WrPtr=%u Level=%u\n",
                             data_type,
                             fifo->m_dwReadPtr, fifo->m_dwWritePtr,
                             fifo->m_dwFifoLevel);

                    /*
                     * The entry just enqueued is at WrPtr-1 (with wraparound).
                     * This is the exact bytes that were written into the FIFO.
                     */
                    if (fifo->m_Fifo && fifo->m_sizeEntry > 0 &&
                        fifo->m_dwFifoLevel > 0) {
                        u32 prev_wr = (fifo->m_dwWritePtr == 0)
                        ? (fifo->m_size - 1)
                        : (fifo->m_dwWritePtr - 1);
                    u32 off = prev_wr * fifo->m_sizeEntry;

                    dev_info(&poc->pdev->dev,
                             "  POST last-enqueued slot[%u] raw bytes:\n",
                             prev_wr);
                    print_hex_dump(KERN_INFO,
                                   "    POST: ", DUMP_PREFIX_OFFSET,
                                   16, 4,
                                   fifo->m_Fifo + off,
                                   min_t(u32, fifo->m_sizeEntry, 128),
                                   false);
                        }
                }
            }
            dev_info(&poc->pdev->dev, "=== END CMD 0x40 ===\n");
        }

    } else {
        CDecoderTask_ProcessArmMessage(task, &msg, status, p0, p1, p2, p3, p4);
    }
}

/* ================================================================
 * CTask_CompleteUser
 * ================================================================ */
int CTask_CompleteUser(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct qp_buffer_descriptor *buf_desc;
    struct c_channel *channel;
    int idx = (int)data_type;

    buf_desc = td->UserBuffer[idx].pBufDesc;
    channel = td->pChannel[idx];

    dev_dbg(&poc->pdev->dev, "CTask_CompleteUser() (0x%px)\n", buf_desc);

    if (!buf_desc)
        return 0;

    if (task->m_pPending[(int)td->direction[idx]] &&
        task->m_pPending[(int)td->direction[idx]]->pTaskData == td) {
        dev_dbg(&poc->pdev->dev, "CTask_CompleteUser() IOPending\n");
    return 0;
        }

        dev_dbg(&poc->pdev->dev,
                "CTask_CompleteUser() ulBufferOffset(%d) ulBufferSize(%d)\n",
                buf_desc->ulBufferOffset,
                buf_desc->ulBufferSize);

        if (td->direction[idx] == CHANNEL_DIR_WRITE &&
            (buf_desc->ulFlags & 0x40000))
            buf_desc->ulFlags &= ~0x40000;

        buf_desc->Status = 0;

        /*
         * Windows clears 0x30 bytes starting at UserBuffer[idx].BUFFER,
         * which in the Windows struct layout encompasses pBufDesc as well.
         * We must clear pBufDesc BEFORE calling CompleteBuffer to prevent
         * double-completion if CompleteBuffer re-enters the streaming path.
         */
        td->UserBuffer[idx].pBufDesc = NULL;
        memset(&td->UserBuffer[idx].BUFFER, 0, sizeof(td->UserBuffer[idx].BUFFER));

        /* Complete buffer to channel - buf_desc is saved locally */
        if (channel && channel->CompleteBuffer)
            ((void (*)(struct c_channel *, struct qp_buffer_descriptor *))
            channel->CompleteBuffer)(channel, buf_desc);

        return 1;
}

void CEncoderTask_ProcessArmMessage(struct c_task *task,
                                    struct host_message *msg,
                                    struct host_message_status *status,
                                    u32 p0, u32 p1, u32 p2, u32 p3, u32 p4)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    u32 cmd = msg->Read & 0xFFFF;
    u32 task_id;
    enum task_data_type data_type;
    enum channel_direction dir;
    struct c_channel *channel = NULL;
    int found;
    int buf_type;
    u32 width = 0x500;
    u32 height = 0x2D0;
    u32 yuv_format[4] = {0, 0, 0, 0};
    u32 fifo_level;
    u32 data_code;
    int ret;

    dev_dbg(&poc->pdev->dev,
            "CEncoderTask_ProcessArmMessage() cmd(0x%x) channel(%d) p1(0x%x) p2(0x%x) p3(0x%x) p4(0x%x) p5(0x%x)\n",
            cmd, msg->Read >> 16, p0, p1, p2, p3, p4);

    /* Check cmd > 0x3F first */
    if (cmd > 0x3F) {
        /* Handle encoder data output requests (0x40, 0x41) */
        if (cmd < 0x42) {
            channel = NULL;
            dir = CHANNEL_DIR_NONE;

            /* Find task and data type */
            if ((poc->codec.m_ChipType & 0xe) == 0) {
                found = CTask_FindTaskDataType(task, TASK_TYPE_ENC, p0 & 0xFFFF, &task_id, &data_type);
            } else {
                task_id = msg->Read >> 16;
                found = CTask_FindDataType(task, task_id, p0 & 0xFFFF, &data_type);
            }

            if (found == 0) {
                QPFWAPI_AckARMMessage(poc, msg, status, 1);
                return;
            }

            channel = task->m_TaskData[task_id].pChannel[(int)data_type];
            dir = task->m_TaskData[task_id].direction[(int)data_type];

            /* Determine buffer type based on data type code - subtract 1 for switch */
            data_code = (p0 & 0xFFFF) - 1;

            switch (data_code) {
                case 0:  /* Original value 1 - COMP_VID */
                    buf_type = 3;  /* YUV MB2RAS */
                    break;

                case 0x80:  /* Original value 0x81 - YUV */
                    /* Check FIFO level for USB bus */
                    if (poc->codec.m_hci.m_bus_type == QPHCI_BUS_USB) {
                        fifo_level = CFifo_GetFifoLevel(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type]);
                        if (fifo_level > 1) {
                            CTask_ReturnArmBuffer(task, msg, status, p0, 1);
                            return;
                        }
                    }

                    if (channel->GetYUVFormat)
                        ((void (*)(struct c_channel *, u32 *))channel->GetYUVFormat)(channel, yuv_format);
                if (channel->GetResolution)
                    ((void (*)(struct c_channel *, u32 *, u32 *))channel->GetResolution)(channel, &width, &height);

                if (yuv_format[0] == 0) {
                    buf_type = 1;  /* YUV planar */
                } else {
                    buf_type = 2;  /* YUV interleaved */
                }
                break;

                case 0x82:  /* Original value 0x83 */
                    if (p2 == task->m_TaskData[task_id].ArmBufferAddr) {
                        dev_dbg(&poc->pdev->dev,
                                ">>>> SAME SIZE REQUEST (addr(0x%x), size(%d)) <<<<<\n",
                                p2, p2);
                    }
                    task->m_TaskData[task_id].ArmBufferAddr = p2;
                    buf_type = 4;
                    break;

                case 1:    /* Original value 2 */
                case 6:    /* Original value 7 */
                case 0x81: /* Original value 0x82 */
                case 0x83: /* Original value 0x84 */
                case 0x84: /* Original value 0x85 */
                case 0x85: /* Original value 0x86 */
                case 0x86: /* Original value 0x87 */
                    buf_type = 4;  /* Others */
                    break;

                default:
                    CTask_ReturnArmBuffer(task, msg, status, p0, 1);
                    return;
            }

            /* Handle compressed audio type from p0 bits 16-23 */
            if ((p0 & 0xFF0000) != 0) {
                u32 audio_type = (p0 & 0xFF0000) >> 16;
                if (audio_type == 0 || audio_type > 4) {
                    dev_dbg(&poc->pdev->dev,
                            "unknown compressed audio type(p1)=0x%x\n", p0);
                } else {
                    task->m_TaskData[task_id].m_audioControlExLPCM =
                    (task->m_TaskData[task_id].m_audioControlExLPCM & 0xFFFFFF) |
                    (audio_type << 24);
                }
            }

            /* Increment ARM request counter */
            task->m_TaskData[task_id].ArmRequestNumber[(int)data_type]++;

            if (cmd == 0x40) {
                dev_dbg(&poc->pdev->dev,
                        "EncDataOutReq task(%d) dataType(%d):#(%d) msg_id(%d) p1(0x%x) p2(0x%x) p3(0x%x) p4(0x%x) p5(0x%x)\n",
                        task_id, data_type,
                        task->m_TaskData[task_id].ArmRequestNumber[(int)data_type],
                        msg->Read >> 16, p0, p1, p2, p3, p4);
            } else {
                dev_dbg(&poc->pdev->dev,
                        "EncDataOutLastReq task(%d) dataType(%d):#(%d) msg_id(%d) p1(0x%x) p2(0x%x) p3(0x%x) p4(0x%x) p5(%d)\n",
                        task_id, data_type,
                        task->m_TaskData[task_id].ArmRequestNumber[(int)data_type],
                        msg->Read >> 16, p0, p1, p2, p3, p4);

                /* Special case for last request with zero values */
                if (p1 == 0 && p2 == 0) {
                    p1 = 0xBEEFCAFE;
                }
            }

            /* Lock task data */
            CObject_Lock(&task->m_TaskData[task_id].m_Object);

            /* Build and queue FIFO entry based on buffer type */
            if (buf_type == 1) {
                /* YUV planar */
                struct {
                    u32 ulYAddr;        /* +0x00 */
                    u32 ulUVAddr;       /* +0x04 */
                    u32 _gap1;          /* +0x08 uninitialized in Windows */
                    u32 ulSize;         /* +0x0C */
                    u32 ulPTS;          /* +0x10 */
                    u32 reserved1;      /* +0x14 = 0 */
                    u32 reserved2;      /* +0x18 = 0 */
                    u32 _gap2;          /* +0x1C uninitialized in Windows */
                    u32 ulPTSValid;     /* +0x20 */
                    u32 ulDataType;     /* +0x24 */
                    u32 ulLast;         /* +0x28 */
                    u32 ulWidth;        /* +0x2C */
                    u32 ulHeight;       /* +0x30 */
                    u32 ulFwData;       /* +0x34 */
                    u32 type;           /* +0x38 = 1 */
                    u32 hm;             /* +0x3C HostMsg.Read */
                    u32 hs;             /* +0x40 HostMsg_status.Read */
                    u32 req_num;        /* +0x44 */
                } fifo_entry;

                memset(&fifo_entry, 0, sizeof(fifo_entry));
                fifo_entry.ulYAddr = p1;
                fifo_entry.ulUVAddr = ((poc->codec.m_ChipType & 0xe) == 0) ?
                (p1 + (p2 >> 16) * 0x40) : p2;
                fifo_entry.ulSize = p3;
                fifo_entry.ulPTS = p4 & 0x7FFFFFFF;
                fifo_entry.reserved1 = 0;
                fifo_entry.reserved2 = 0;
                fifo_entry.ulPTSValid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
                fifo_entry.ulDataType = p0 & 0xFFFF;
                fifo_entry.ulLast = (cmd == 0x41) ? 1 : 0;
                fifo_entry.ulWidth = width;
                fifo_entry.ulHeight = height;
                fifo_entry.ulFwData = p0 >> 24;
                fifo_entry.type = 1;
                fifo_entry.hm = msg->Read;
                fifo_entry.hs = status->Read;
                fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

                CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                              &fifo_entry);
            }
            else if (buf_type == 2) {
                /* YUV interleaved - SAME layout as buf_type 1, different UV calc */
                struct {
                    u32 ulYAddr;        /* +0x00 */
                    u32 ulUVAddr;       /* +0x04 */
                    u32 _gap1;          /* +0x08 */
                    u32 ulSize;         /* +0x0C */
                    u32 ulPTS;          /* +0x10 */
                    u32 reserved1;      /* +0x14 = 0 */
                    u32 reserved2;      /* +0x18 = 0 */
                    u32 reserved3;      /* +0x1C = 0 */
                    u32 ulPTSValid;     /* +0x20 */
                    u32 ulDataType;     /* +0x24 */
                    u32 ulLast;         /* +0x28 */
                    u32 ulWidth;        /* +0x2C */
                    u32 ulHeight;       /* +0x30 */
                    u32 ulFwData;       /* +0x34 */
                    u32 type;           /* +0x38 = 2 */
                    u32 hm;             /* +0x3C */
                    u32 hs;             /* +0x40 */
                    u32 req_num;        /* +0x44 */
                } fifo_entry;

                memset(&fifo_entry, 0, sizeof(fifo_entry));
                fifo_entry.ulYAddr = p1;
                fifo_entry.ulUVAddr = ((poc->codec.m_ChipType & 0xe) == 0) ?
                (p1 + (p2 >> 16) * 0x40) : p2;
                fifo_entry.ulSize = p3;
                fifo_entry.ulPTS = p4 & 0x7FFFFFFF;
                fifo_entry.reserved1 = 0;
                fifo_entry.reserved2 = 0;
                fifo_entry.reserved3 = 0;
                fifo_entry.ulPTSValid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
                fifo_entry.ulDataType = p0 & 0xFFFF;
                fifo_entry.ulLast = (cmd == 0x41) ? 1 : 0;
                fifo_entry.ulWidth = width;
                fifo_entry.ulHeight = height;
                fifo_entry.ulFwData = p0 >> 24;
                fifo_entry.type = 2;
                fifo_entry.hm = msg->Read;
                fifo_entry.hs = status->Read;
                fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

                CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                              &fifo_entry);
            }
            else if (buf_type == 3) {
                /* YUVRAS */
                struct {
                    u32 ulYAddr;        /* +0x00 */
                    u32 ulUAddr;        /* +0x04 */
                    u32 ulVAddr;        /* +0x08 */
                    u32 ulSize;         /* +0x0C */
                    u32 ulPTS;          /* +0x10 */
                    u32 reserved1;      /* +0x14 = 0 */
                    u32 reserved2;      /* +0x18 = 0 */
                    u32 reserved3;      /* +0x1C = 0 */
                    u32 ulPTSValid;     /* +0x20 */
                    u32 ulDataType;     /* +0x24 */
                    u32 ulLast;         /* +0x28 */
                    u32 _gap1;          /* +0x2C */
                    u32 _gap2;          /* +0x30 */
                    u32 ulFwData;       /* +0x34 */
                    u32 type;           /* +0x38 = 3 */
                    u32 hm;             /* +0x3C */
                    u32 hs;             /* +0x40 */
                    u32 req_num;        /* +0x44 */
                } fifo_entry;

                memset(&fifo_entry, 0, sizeof(fifo_entry));
                fifo_entry.ulYAddr = p1;
                fifo_entry.ulUAddr = ((poc->codec.m_ChipType & 0xe) == 0) ?
                (p1 + (p2 >> 15)) : p2;
                fifo_entry.ulVAddr = ((poc->codec.m_ChipType & 0xe) == 0) ?
                (fifo_entry.ulUAddr + (p2 & 0x7FFF)) :
                (p2 + ((p3 + 0x3FF) >> 10) * 0x100);
                fifo_entry.ulSize = p3;
                fifo_entry.ulPTS = p4 & 0x7FFFFFFF;
                fifo_entry.reserved1 = 0;
                fifo_entry.reserved2 = 0;
                fifo_entry.reserved3 = 0;
                fifo_entry.ulPTSValid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
                fifo_entry.ulDataType = p0 & 0xFFFF;
                fifo_entry.ulLast = (cmd == 0x41) ? 1 : 0;
                fifo_entry.ulFwData = p0 >> 24;
                fifo_entry.type = 3;
                fifo_entry.hm = msg->Read;
                fifo_entry.hs = status->Read;
                fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

                CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                              &fifo_entry);
            }
            else {
                /* OTHERS (buf_type 4) */
                struct {
                    u32 ulAddr;             /* +0x00 */
                    u32 _uninit1;           /* +0x04 */
                    u32 _uninit2;           /* +0x08 */
                    u32 ulSize;             /* +0x0C */
                    u32 ulPTS;              /* +0x10 */
                    u32 reserved1;          /* +0x14 = 0 */
                    u32 _gap1;              /* +0x18 */
                    u32 ulCompressAudioType;/* +0x1C */
                    u32 ulPTSValid;         /* +0x20 */
                    u32 ulDataType;         /* +0x24 */
                    u32 ulLast;             /* +0x28 */
                    u32 reserved2;          /* +0x2C = 0 */
                    u32 ulWrapAround;       /* +0x30 */
                    u32 ulFwData;           /* +0x34 */
                    u32 type;               /* +0x38 = 4 */
                    u32 hm;                 /* +0x3C */
                    u32 hs;                 /* +0x40 */
                    u32 req_num;            /* +0x44 */
                } fifo_entry;

                memset(&fifo_entry, 0, sizeof(fifo_entry));
                fifo_entry.ulAddr = p1;
                fifo_entry.ulSize = p3;
                fifo_entry.ulPTS = p4 & 0x7FFFFFFF;
                fifo_entry.reserved1 = 0;
                fifo_entry.ulCompressAudioType = (p0 >> 16) & 0xFF;
                fifo_entry.ulPTSValid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
                fifo_entry.ulDataType = p0 & 0xFFFF;
                fifo_entry.ulLast = (cmd == 0x41) ? 1 : 0;
                fifo_entry.reserved2 = 0;
                fifo_entry.ulWrapAround = p2 & 1;
                fifo_entry.ulFwData = p0 >> 24;
                fifo_entry.type = 4;
                fifo_entry.hm = msg->Read;
                fifo_entry.hs = status->Read;
                fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

                CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                              &fifo_entry);

            }

            /* Unlock task data */
            CObject_Unlock(&task->m_TaskData[task_id].m_Object);

            /* Acknowledge ARM message */
            QPFWAPI_AckARMMessage(poc, msg, status, 1);

            dev_dbg(&poc->pdev->dev,
                    "m_State=%d ProcessDataStreaming=%p\n",
                    task->m_TaskData[task_id].m_State,
                    task->ProcessDataStreaming);

            /* Process data or drain FIFO */
            if (task->m_TaskData[task_id].m_State != TASK_STATE_IDLE) {
                dev_dbg(&poc->pdev->dev, "Calling ProcessDataStreaming\n");
                if (task->ProcessDataStreaming)
                    ((void (*)(struct c_task *))task->ProcessDataStreaming)(task);
            } else {
                dev_dbg(&poc->pdev->dev, "IDLE path - draining FIFO\n");

                /* Task is idle - drain the FIFO */
                CObject_Lock(&task->m_TaskData[task_id].m_Object);

                do {
                    if (task->CompleteArm)
                        ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                        task->CompleteArm)(task, &task->m_TaskData[task_id], data_type);

                    ret = CFifo_GetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                                        &task->m_TaskData[task_id].ArmRequest[(int)data_type]);
                } while (ret != 0);

                CObject_Unlock(&task->m_TaskData[task_id].m_Object);
            }
            return;
        }

        /* Handle encoder status message replies (0x50) */
        if (cmd == 0x50) {
            u32 has_valid_flag = (p0 & 0x80000000) != 0;

            if ((poc->codec.m_ChipType & 0xe) == 0) {
                task_id = status->Read >> 16;
            } else {
                task_id = msg->Read >> 16;
            }

            u32 param1 = p0 & 0x7FFFFFFF;

            dev_dbg(&poc->pdev->dev,
                    "EncStaMsgReply param1=0x%x taskId=%u\n", p0, task_id);

            if (param1 == 2) {
                /* Stop compress */
                if (poc->codec.m_ChipType == 1) {
                    if (p1 == 0) {
                        ret = CTask_FindTask(task, TASK_TYPE_ENC, TASK_DATA_TYPE_COMP_VID, &task_id);
                        if (ret == 0) {
                            CTask_FindTask(task, TASK_TYPE_ENC, TASK_DATA_TYPE_COMP_AUD, &task_id);
                        }
                    } else if (p1 == 1) {
                        CTask_FindTask(task, TASK_TYPE_ENC, TASK_DATA_TYPE_YUV, &task_id);
                    }
                }
                dev_dbg(&poc->pdev->dev, "EncStaMsgReply StopCompress taskId=%u param2=%u\n",
                        task_id, p1);
                QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x10);
            }
            else if (param1 == 7) {
                /* Get VIOSD table addresses */
                dev_dbg(&poc->pdev->dev, "EncStaMsgReply GetViosdTableAddr(0x%x, 0x%x)\n",
                        p1, p2);
                task->m_TaskData[task_id].m_encFontTableAddr = p2 << 2;
                task->m_TaskData[task_id].m_encTextListAddr = p1 << 2;
                task->m_TaskData[task_id].m_encSyncTimeAddr = p3 << 2;
                QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x80);
            }
            else if (param1 == 8) {
                /* Get current video buffer */
                dev_dbg(&poc->pdev->dev, "EncStaMsgReply GetCurVidBuf addr=0x%x size=%u\n",
                        p1, p2);

                if (has_valid_flag == 0) {
                    task->m_TaskData[task_id].m_encVidBufLumaAddr = 0xFFFFFFFF;
                } else {
                    /* Read video buffer info from memory */
                    u32 addr = p1 << 2;

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr,
                        &task->m_TaskData[task_id].m_encVidBufLumaAddr);

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr + 4,
                        &task->m_TaskData[task_id].m_encVidBufChromaAddr);

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr + 8,
                        &task->m_TaskData[task_id].m_encFrameLABufAddr);

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr + 0xc,
                        &task->m_TaskData[task_id].m_encTopLABufAddr);

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr + 0x10,
                        &task->m_TaskData[task_id].m_encBottomLABufAddr);

                    ((int (*)(struct ihciapi *, u32, u32 *))
                    poc->codec.m_hci.MemoryRead)(
                        &poc->codec.m_hci, addr + 0x14,
                        &task->m_TaskData[task_id].m_encVidBufPTS);
                }
                QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x100);
            }
            else if (param1 == 0x2F) {
                /* Encoder misc message */
                dev_dbg(&poc->pdev->dev, "EncStaMsgReply EncMiscMessage param2=%u\n", p1);
                if (p1 == 9) {
                    QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x40);
                }
            }

            QPFWAPI_AckARMMessage(poc, msg, status, 1);
            return;
        }

        /* Handle encoder error report (0x51) */
        if (cmd == 0x51) {
            dev_err(&poc->pdev->dev, "EncErrorReport param1=%u!!\n", p0);
            QPFWAPI_AckARMMessage(poc, msg, status, 1);
            return;
        }
    }

    /* Unknown command */
    dev_dbg(&poc->pdev->dev, "Unknown Command 0x%x\n", cmd);
    QPFWAPI_AckARMMessage(poc, msg, status, 1);
}

void CDecoderTask_ProcessArmMessage(struct c_task *task,
                                    struct host_message *msg,
                                    struct host_message_status *status,
                                    u32 p0, u32 p1, u32 p2, u32 p3, u32 p4)
{
    struct c985_poc *poc = codec_to_poc(task->m_pMpegCodec);
    struct c_channel *channel = NULL;
    struct c_channel *audio_channel = NULL;
    enum channel_direction dir = CHANNEL_DIR_NONE;
    enum task_data_type data_type;
    u32 cmd = msg->Read & 0xFFFF;
    u32 task_id;
    u32 callback_event = 0;
    u32 width = 0x500;
    u32 height = 0x2D0;
    u32 yuv_format[2] = {0, 0};
    int found;
    int buf_type;
    int valid_info;
    int ret;

    dev_dbg(&poc->pdev->dev,
            "CDecoderTask_ProcessArmMessage() cmd(0x%x) channel(%d)\n",
            cmd, msg->Read >> 16);

    /* ========================================
     * Command 0xD0 - Decoder Status Message Reply
     * ======================================== */
    if (cmd == 0xD0) {
        if ((poc->codec.m_ChipType & 0xe) == 0) {
            task_id = status->Read >> 16;
        } else {
            task_id = msg->Read >> 16;
        }

        dev_dbg(&poc->pdev->dev,
                "DecStaMsgReply param1=%u taskId=%u\n", p0, task_id);

        if (p0 == 0x82) {
            /* Stop decompress */
            if (poc->codec.m_ChipType == 1) {
                ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_VID, &task_id);
                if (ret == 0) {
                    CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_AUD, &task_id);
                }
            }
            dev_dbg(&poc->pdev->dev,
                    "DecStaMsgReply StopDecompress taskId=%u\n", task_id);
            QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x10);
        }
        else if (p0 == 0x87) {
            /* Get play info */
            valid_info = 1;

            switch (p1) {
                case 1:
                    dev_dbg(&poc->pdev->dev, "DecGetPlayInfo QPDEC_INFO_TYPE_MASTER_CLK\n");
                    break;
                case 2:
                    dev_dbg(&poc->pdev->dev, "DecGetPlayInfo QPDEC_INFO_TYPE_CUR_DISP_PTS\n");
                    break;
                case 3:
                    dev_dbg(&poc->pdev->dev, "DecGetPlayInfo QPDEC_INFO_TYPE_DISPLAYED_CNT\n");
                    break;
                case 4:
                    dev_dbg(&poc->pdev->dev, "DecGetPlayInfo QPDEC_INFO_TYPE_DECODED_CNT\n");
                    break;
                case 5:
                    dev_dbg(&poc->pdev->dev,
                            "DecGetPlayInfo QPDEC_INFO_TYPE_GET_SHARE_MEM wr=0x%x rd=0x%x\n",
                            p2, p3);
                    task->m_TaskData[task_id].m_decFWSharedMemWr = p2;
                    task->m_TaskData[task_id].m_decFWSharedMemRd = p3;
                    break;
                default:
                    dev_dbg(&poc->pdev->dev, "DecGetPlayInfo unknown(%u)\n", p1);
                    valid_info = 0;
                    break;
            }

            if (valid_info) {
                QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x20);
            }
        }
        else if (p0 == 0xB0) {
            /* Get OSD memory address */
            dev_dbg(&poc->pdev->dev, "GetOsdMemAddr start_addr=0x%x\n", p1);
            task->m_TaskData[task_id].m_decFWSharedMemWr = p1;
            QPOSMSetEvtgrp(&task->m_TaskData[task_id].m_EvtReply, 0x20);
        }

        QPFWAPI_AckARMMessage(poc, msg, status, 1);
        return;
    }

    /* ========================================
     * Command 0xD1 - Decoder Status Report
     * ======================================== */
    if (cmd == 0xD1) {
        channel = NULL;
        audio_channel = NULL;
        callback_event = 0;

        dev_dbg(&poc->pdev->dev,
                "DecStatusReport param1=%u param2=%u\n", p0, p1);

        QPFWAPI_AckARMMessage(poc, msg, status, 1);

        if (p0 == 0) {
            if (p1 == 1) {
                /* Video sequence start */
                if ((poc->codec.m_ChipType & 0xe) == 0) {
                    ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_VID, &task_id);
                    if (ret != 0) {
                        channel = task->m_TaskData[task_id].pChannel[0];
                    }
                } else {
                    task_id = msg->Read >> 16;
                    channel = task->m_TaskData[task_id].pChannel[0];
                }
                callback_event = 4;
            }
            else if (p1 == 2) {
                /* Video sequence end */
                if ((poc->codec.m_ChipType & 0xe) == 0) {
                    ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_VID, &task_id);
                    if (ret != 0) {
                        channel = task->m_TaskData[task_id].pChannel[0];
                    }
                } else {
                    task_id = msg->Read >> 16;
                    channel = task->m_TaskData[task_id].pChannel[0];
                }
                callback_event = 1;
            }
        }
        else if (p0 == 1) {
            /* Audio info */
            if ((poc->codec.m_ChipType & 0xe) == 0) {
                ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_VID, &task_id);
                if (ret != 0) {
                    channel = task->m_TaskData[task_id].pChannel[0];
                }
            } else {
                task_id = msg->Read >> 16;
                channel = task->m_TaskData[task_id].pChannel[0];
            }
            callback_event = 8;
        }
        else if (p0 == 2) {
            /* End of stream */
            if ((poc->codec.m_ChipType & 0xe) == 0) {
                ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_VID, &task_id);
                if (ret != 0) {
                    channel = task->m_TaskData[task_id].pChannel[0];
                }
                ret = CTask_FindTask(task, TASK_TYPE_DEC, TASK_DATA_TYPE_COMP_AUD, &task_id);
                if (ret != 0) {
                    audio_channel = task->m_TaskData[task_id].pChannel[1];
                }
            } else {
                task_id = msg->Read >> 16;
                channel = task->m_TaskData[task_id].pChannel[0];
                audio_channel = task->m_TaskData[task_id].pChannel[1];
            }
            callback_event = 0x10;
            task->m_TaskData[task_id].bDone = 1;
        }

        if (channel) {
            CChannel_DeviceCallback(channel, callback_event, NULL);
        }
        if (audio_channel) {
            CChannel_DeviceCallback(audio_channel, callback_event, NULL);
        }
        return;
    }

    /* ========================================
     * Command 0xC0 - Decoder Data Transfer Request
     * ======================================== */
    if (cmd != 0xC0) {
        dev_dbg(&poc->pdev->dev, "Unknown Command 0x%x\n", cmd);
        QPFWAPI_AckARMMessage(poc, msg, status, 1);
        return;
    }

    /* Find task data type */
    if ((poc->codec.m_ChipType & 0xe) == 0) {
        found = CTask_FindTaskDataType(task, TASK_TYPE_DEC, p0 & 0xFFFF, &task_id, &data_type);
    } else {
        task_id = msg->Read >> 16;
        found = CTask_FindDataType(task, task_id, p0 & 0xFFFF, &data_type);
    }

    if (found == 0) {
        QPFWAPI_AckARMMessage(poc, msg, status, 1);
        return;
    }

    channel = task->m_TaskData[task_id].pChannel[(int)data_type];
    dir = task->m_TaskData[task_id].direction[(int)data_type];

    /* Determine buffer type based on data type code */
    u32 data_code = p0 & 0xFFFF;

    if (data_code < 2) {
        CTask_ReturnArmBuffer(task, msg, status, p0, 1);
        return;
    }

    if (data_code > 5) {
        if (data_code == 0x81) {
            /* YUV buffer request */
            if (channel->GetYUVFormat)
                ((void (*)(struct c_channel *, u32 *))channel->GetYUVFormat)(channel, yuv_format);
            if (channel->GetResolution)
                ((void (*)(struct c_channel *, u32 *, u32 *))channel->GetResolution)(channel, &width, &height);

            if (yuv_format[0] == 0) {
                buf_type = 1;  /* YUV planar */
            } else {
                buf_type = 2;  /* YUV interleaved */
            }
        } else if (data_code == 0x82) {
            buf_type = 4;  /* Others */
        } else {
            CTask_ReturnArmBuffer(task, msg, status, p0, 1);
            return;
        }
    } else {
        buf_type = 4;  /* Others */
    }

    /* Increment ARM request counter */
    task->m_TaskData[task_id].ArmRequestNumber[(int)data_type]++;

    dev_dbg(&poc->pdev->dev,
            "DecDataXferReq task(%u) dataType(%d):#(%d) msg_id(%u) p1=0x%x p2=0x%x p3=0x%x p4=0x%x p5=%u\n",
            task_id, data_type,
            task->m_TaskData[task_id].ArmRequestNumber[(int)data_type],
            msg->Read >> 16, p0, p1, p2, p3, p4);

    /* Lock task data */
    CObject_Lock(&task->m_TaskData[task_id].m_Object);

    /* Build ARM request based on buffer type and add to FIFO */
    if (buf_type == 1) {
        /* YUV planar */
        struct {
            u32 y_addr;
            u32 uv_addr;
            u32 size;
            u32 pts;
            u32 reserved1;
            u32 reserved2;
            u32 pts_mask;
            u32 pts_valid;
            u32 last;
            u32 width;
            u32 height;
            u32 fw_data;
            struct host_message msg;
            struct host_message_status status;
            int req_num;
        } fifo_entry;

        fifo_entry.y_addr = p1;
        if ((poc->codec.m_ChipType & 0xe) == 0) {
            fifo_entry.uv_addr = p1 + (p2 >> 16) * 0x40;
        } else {
            fifo_entry.uv_addr = p2;
        }
        fifo_entry.size = p3;
        fifo_entry.reserved1 = 0;
        fifo_entry.reserved2 = 0;
        fifo_entry.pts_mask = p4 & 0x7FFFFFFF;
        fifo_entry.pts_valid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
        fifo_entry.last = 0;
        fifo_entry.width = width;
        fifo_entry.height = height;
        fifo_entry.fw_data = p0 >> 24;
        fifo_entry.msg = *msg;
        fifo_entry.status = *status;
        fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

        CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type], &fifo_entry.y_addr);
    }
    else if (buf_type == 2) {
        /* YUV interleaved */
        struct {
            u32 y_addr;
            u32 uv_addr;
            u32 size;
            u32 pts;
            u32 reserved1;
            u32 reserved2;
            u32 reserved3;
            u32 pts_mask;
            u32 pts_valid;
            u32 last;
            u32 width;
            u32 height;
            u32 fw_data;
            struct host_message msg;
            struct host_message_status status;
            int req_num;
        } fifo_entry;

        fifo_entry.y_addr = p1;
        if ((poc->codec.m_ChipType & 0xe) == 0) {
            fifo_entry.uv_addr = p1 + (p2 >> 16) * 0x40;
        } else {
            fifo_entry.uv_addr = p2;
        }
        fifo_entry.size = p3;
        fifo_entry.reserved1 = 0;
        fifo_entry.reserved2 = 0;
        fifo_entry.reserved3 = 0;
        fifo_entry.pts_mask = p4 & 0x7FFFFFFF;
        fifo_entry.pts_valid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
        fifo_entry.last = 0;
        fifo_entry.width = width;
        fifo_entry.height = height;
        fifo_entry.fw_data = p0 >> 24;
        fifo_entry.msg = *msg;
        fifo_entry.status = *status;
        fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

        CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type], &fifo_entry.y_addr);
    }
    else {
        /* Others (buf_type == 4) */
        struct {
            u32 addr;
            u32 size;
            u32 reserved1;
            u32 pts_mask;
            u32 pts_valid;
            u32 last;
            u32 compressed_type;
            u32 reserved2;
            u32 fw_data;
            struct host_message msg;
            struct host_message_status status;
            int req_num;
        } fifo_entry;

        fifo_entry.addr = p1;
        fifo_entry.size = p3;
        fifo_entry.reserved1 = 0;
        fifo_entry.pts_mask = p4 & 0x7FFFFFFF;
        fifo_entry.pts_valid = (dir == CHANNEL_DIR_READ && (p4 & 0x80000000)) ? 1 : 0;
        fifo_entry.last = 0;
        fifo_entry.compressed_type = p2 & 1;
        fifo_entry.reserved2 = 0;
        fifo_entry.fw_data = p0 >> 24;
        fifo_entry.msg = *msg;
        fifo_entry.status = *status;
        fifo_entry.req_num = task->m_TaskData[task_id].ArmRequestNumber[(int)data_type];

        CFifo_SetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type], &fifo_entry.addr);
    }

    /* Unlock task data */
    CObject_Unlock(&task->m_TaskData[task_id].m_Object);

    /* Acknowledge ARM message */
    QPFWAPI_AckARMMessage(poc, msg, status, 1);

    /* Process data or drain FIFO */
    if (task->m_TaskData[task_id].m_State != TASK_STATE_IDLE) {
        if (task->ProcessDataStreaming)
            ((void (*)(struct c_task *))task->ProcessDataStreaming)(task);
    } else {
        /* Task is idle - drain the FIFO */
        CObject_Lock(&task->m_TaskData[task_id].m_Object);

        do {
            if (task->CompleteArm)
                ((void (*)(struct c_task *, struct task_data *, enum task_data_type))
                task->CompleteArm)(task, &task->m_TaskData[task_id], data_type);

            ret = CFifo_GetFifo(task->m_TaskData[task_id].pArmMsgFifo[(int)data_type],
                                &task->m_TaskData[task_id].ArmRequest[(int)data_type]);
        } while (ret != 0);

        CObject_Unlock(&task->m_TaskData[task_id].m_Object);
    }
}

void CTask_ReturnArmBuffer(struct c_task *task, struct host_message *msg,
                           struct host_message_status *status, u32 param, int flag)
{
}
