/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ctask.h - CTask public API for AVerMedia C985
 *
 * This header exposes the public interface for task management.
 * Internal details are in ctask_private.h
 */
#ifndef CTASK_H
#define CTASK_H

#include <linux/types.h>
#include "../structs.h"
#include "../types.h"
#include "../include/abi/ccapturefilter.h"
#include "../include/abi/cencoderfilter.h"
#include "../include/abi/cdecoderfilter.h"
#include "../include/abi/qp_process_type.h"

struct CTaskRawAudio;


/* ============================================
 * Constants
 * ============================================ */
#define MAX_TASKS 8

/* Thread event flags - from Ghidra */
#define THREAD_EVENT_EXIT           0x001
#define THREAD_EVENT_ARM_MSG        0x004  /* ProcessArmMessage */
#define THREAD_EVENT_DMA_READ       0x008  /* CompleteData(0) */
#define THREAD_EVENT_DMA_WRITE      0x010  /* CompleteData(1) */
#define THREAD_EVENT_PROCESS_DATA   0x020  /* ProcessDataStreaming */
#define THREAD_EVENT_CANCEL_BUFFER  0x040  /* ProcessCancelBuffer */
#define THREAD_EVENT_FLUSH          0x080  /* ProcessFlush */
#define THREAD_EVENT_FLUSH_ARM      0x100  /* ProcessFlushArm */
#define THREAD_EVENT_ALL            0x1FF

/* Reply event flags */
#define REPLY_EVENT_EXIT            0x001
#define REPLY_EVENT_ARM_MSG         0x004
#define REPLY_EVENT_UNKNOWN_08      0x008

/* Task data reply events */
#define TASK_REPLY_EVENT_STOP       0x010
#define TASK_REPLY_EVENT_PLAYINFO   0x020
#define TASK_REPLY_EVENT_MISC       0x040
#define TASK_REPLY_EVENT_VIOSD      0x080
#define TASK_REPLY_EVENT_VIDBUF     0x100

/* ============================================
 * Constructor / Destructor
 * ============================================ */
struct c_task *CTask_Constructor(struct c_task *task, struct CObject *parent,
                                 u32 param_3, u32 param_4, struct cql_codec *codec,
                                 void *evt_wait, void *evt_reply);
void CTask_Destructor(struct c_task *task);

/* ============================================
 * Task Initialization
 * ============================================ */
int CTask_InitTasks(struct c_task *task);
void CTask_DoneTasks(struct c_task *task);

/* ============================================
 * Task Allocation / Management
 * ============================================ */
int CTask_Alloc(struct c_task *task, enum task_type type, u32 *task_id);
int CTask_Release(struct c_task *task, u32 task_id);
enum task_state CTask_GetTaskState(struct c_task *task, u32 task_id);

/* ============================================
 * Task Lifecycle
 * ============================================ */
int CTask_Open(struct c_task *task, u32 task_id, enum task_data_type data_type,
               enum channel_direction dir, u32 fw_data_type, struct c_channel *channel);
int CTask_Close(struct c_task *task, u32 task_id, enum task_data_type data_type,
                int lock_main);
int CTask_Start(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_Stop(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_Acquire(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_Pause(struct c_task *task, u32 task_id);
int CTask_Resume(struct c_task *task, u32 task_id);

/* ============================================
 * Buffer Management
 * ============================================ */
int CTask_CancelBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_NewBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_Flush(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_FlushArm(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_DMARequest(struct c_task *task, struct task_dma_request *req);

/* ============================================
 * IO Block Operations
 * ============================================ */
void CTask_BuildIoBlock(struct c_task *task, struct task_data *td,
                        enum task_data_type data_type);
void CTask_SendIoBlock(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type);
void CTask_BuildIoBlockYUV(struct c_task *task, struct task_data *td,
                           enum task_data_type data_type);
void CTask_BuildIoBlockYUVMB2RAS(struct c_task *task, struct task_data *td,
                                 enum task_data_type data_type);
void CTask_BuildIoBlockYUVRAS(struct c_task *task, struct task_data *td,
                              enum task_data_type data_type);
int CTask_BuildIoBlockSideBandDMA(struct c_task *task, struct task_dma_request *req);
void CTask_SendIoBlockSideBandDMA(struct c_task *task, struct task_dma_request *req);

/* ============================================
 * IO Completion
 * ============================================ */
void CTask_CompleteData(struct c_task *task, enum channel_direction dir);
int CTask_CompleteUser(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type);
void CTask_CompleteArm(struct c_task *task, struct task_data *td,
                       enum task_data_type data_type);
void CTask_ProcessIoComplete(struct c_task *task, enum channel_direction dir);

/* ============================================
 * ARM Message Processing
 * ============================================ */
void CTask_ProcessArmMessage(struct c_task *task);
void CEncoderTask_ProcessArmMessage(struct c_task *task,
                                    struct host_message *msg,
                                    struct host_message_status *status,
                                    u32 p0, u32 p1, u32 p2, u32 p3, u32 p4);
void CDecoderTask_ProcessArmMessage(struct c_task *task,
                                    struct host_message *msg,
                                    struct host_message_status *status,
                                    u32 p0, u32 p1, u32 p2, u32 p3, u32 p4);

/* ============================================
 * Encoder/Decoder IO Complete
 * ============================================ */
void CEncoderTask_ProcessIoComplete(struct c_task *task, struct task_data *td,
                                    enum task_data_type data_type, int status);
void CDecoderTask_ProcessIoComplete(struct c_task *task, struct task_data *td,
                                    enum task_data_type data_type, int status);

/* ============================================
 * Encoder/Decoder Start
 * ===================================_EQPErrors========= */
int CEncoderTask_Start(struct c_task *task, u32 task_id);
int CDecoderTask_Start(struct c_task *task, u32 task_id);

/* ============================================
 * Data Streaming / Processing
 * ============================================ */
void CTask_ProcessDataStreaming(struct c_task *task);
int CTask_ProcessCancelBuffer(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_ProcessFlush(struct c_task *task, u32 task_id, enum task_data_type data_type);
int CTask_ProcessFlushArm(struct c_task *task, u32 task_id, enum task_data_type data_type);

/* ============================================
 * Task Search Functions
 * ============================================ */
int CTask_FindTaskDataType(struct c_task *task, enum task_type type,
                           u32 id, u32 *task_id, enum task_data_type *data_type);
int CTask_FindDataType(struct c_task *task, u32 task_id,
                       u32 id, enum task_data_type *data_type);
int CTask_FindTask(struct c_task *task, enum task_type type,
                   enum task_data_type data_type, u32 *task_id);

/* ============================================
 * ARM Buffer Handling
 * ============================================ */
void CTask_ReturnArmBuffer(struct c_task *task, struct host_message *msg,
                           struct host_message_status *status, u32 param, int flag);

/* ============================================
 * Settings
 * ============================================ */
void CTask_LoadDefaultSettings(struct c_task *task, struct task_data *td);
void CTask_ResetState(struct c_task *task, struct task_data *td);

/* ============================================
 * Thread
 * ============================================ */
int CTask_ThreadProc(void *data);
struct c_thread *CThread_Constructor(struct c_thread *thread, const char *name,
                                     void *context, u32 priority,
                                     void *evt_wait, void *evt_reply, int flag);
int CThread_ThreadInit(struct c_thread *thread);
void CThread_ThreadDone(struct c_thread *thread);

/* ============================================
 * FIFO
 * ============================================ */
struct c_fifo *CFifo_Constructor(struct c_fifo *fifo, struct CObject *parent,
                                 u32 attr, u32 size, u32 entry_size);
void CFifo_Destructor(struct c_fifo *fifo);
int CFifo_GetFifo(struct c_fifo *fifo, void *entry);
int CFifo_SetFifo(struct c_fifo *fifo, void *entry);
u32 CFifo_GetFifoLevel(struct c_fifo *fifo);

/* ============================================
 * Channel
 * ============================================ */
int CChannel_needByteSwapping(struct c_channel *ch);

/* ============================================
 * Helpers
 * ============================================ */
int CTask_isIOReady(struct c_task *task, enum channel_direction dir);
int CTask_UseVideoInputDevice(struct c_task *task, struct task_data *td);
int CTask_NeedVideoInputDevice(struct c_task *task);

ulong CTaskRawAudio_getTaskHandle(struct CTaskRawAudio *this);
ulong CDecoderFilter_GetTaskHandle(struct CDecoderFilter *this);
ulong CEncoderFilter_GetEncodeTaskHandle(struct CEncoderFilter *this);
ulong CCaptureFilter_GetRawVideoTaskHandle(struct CCaptureFilter *this);
ulong CCaptureFilter_GetRawAudioTaskHandle(struct CCaptureFilter *this);
enum QP_PROCESS_TYPE CEncoderFilter_getProcessName(struct CEncoderFilter *filter);
int CCaptureFilter_ProcessName_Setting(struct CCaptureFilter *filter, enum QP_PROCESS_TYPE process_type);


#endif /* CTASK_H */
