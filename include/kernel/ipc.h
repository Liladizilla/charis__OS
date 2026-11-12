/* ipc.h - task-owned, rights-checked IPC endpoints for CharisOS. */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>

typedef u64 ipc_handle_t;
#define IPC_MAX_CHANNELS       32U
#define IPC_MAX_MSG_SIZE       512U
#define IPC_MAX_MESSAGES       16U
#define IPC_HANDLE_SLOT_BITS       4U
#define IPC_HANDLE_SLOT_MASK       ((1ULL << IPC_HANDLE_SLOT_BITS) - 1ULL)
#define IPC_HANDLE_GENERATION_MAX  ((1U << 28) - 1U)
#define IPC_HANDLE_INVALID        ((ipc_handle_t)0)

#define IPC_RIGHT_SEND       (1U << 0)
#define IPC_RIGHT_RECEIVE    (1U << 1)
#define IPC_RIGHT_MASK       (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE)

#define IPC_STATUS_OK             0
#define IPC_STATUS_ERROR         -1
#define IPC_STATUS_EMPTY         -2
#define IPC_STATUS_FULL          -3
#define IPC_STATUS_BUFFER_SMALL  -4
#define IPC_STATUS_PEER_CLOSED   -5

void ipc_init(void);

/* Kernel-only: atomically install one endpoint in each live kernel task. */
bool ipc_endpoint_pair_create(task_t* first_task, task_t* second_task,
                              u32 first_rights, u32 second_rights,
                              ipc_handle_t* first_handle,
                              ipc_handle_t* second_handle);
int ipc_send(ipc_handle_t endpoint, const void* data, u64 size);
int ipc_recv(ipc_handle_t endpoint, void* buffer, u64 capacity,
             u32* sender_pid);
int ipc_close(ipc_handle_t endpoint);
void ipc_task_close_all(task_t* task);

/* Shared memory remains a separate kernel-only API, not IPC authority. */
#define SHM_MAX_BLOCKS      16
#define SHM_BLOCK_SIZE      4096
int shm_alloc(void);
void* shm_get(int id);
void shm_free(int id);
