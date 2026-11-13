/* ipc.h - task-owned, rights-checked IPC endpoints for CharisOS. */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>

typedef u64 ipc_handle_t;
typedef u64 ipc_service_handle_t;
typedef u64 ipc_request_handle_t;

#define IPC_MAX_SERVICES             16U
#define IPC_MAX_SERVICE_NAME         32U
#define IPC_MAX_CONNECT_REQUESTS     32U
#define IPC_SERVICE_SLOT_BITS         4U
#define IPC_SERVICE_SLOT_MASK         ((1ULL << IPC_SERVICE_SLOT_BITS) - 1ULL)
#define IPC_SERVICE_GENERATION_MAX    ((1U << 28) - 1U)
#define IPC_REQUEST_SLOT_BITS         5U
#define IPC_REQUEST_SLOT_MASK         ((1ULL << IPC_REQUEST_SLOT_BITS) - 1ULL)
#define IPC_REQUEST_GENERATION_MAX    ((1U << 27) - 1U)

#define IPC_STATUS_REJECTED          -7
#define IPC_STATUS_DENIED            -8
#define IPC_STATUS_LIMIT             -9

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
#define IPC_STATUS_CANCELLED     -6

#define IPC_WAIT_STATUS_IDLE          0
#define IPC_WAIT_STATUS_PENDING       1
#define IPC_WAIT_STATUS_MESSAGE       2
#define IPC_WAIT_STATUS_PEER_CLOSED   3
#define IPC_WAIT_STATUS_CANCELLED     4

void ipc_init(void);

/* Kernel-only: atomically install one endpoint in each live kernel task. */
bool ipc_endpoint_pair_create(task_t* first_task, task_t* second_task,
                              u32 first_rights, u32 second_rights,
                              ipc_handle_t* first_handle,
                              ipc_handle_t* second_handle);
int ipc_send(ipc_handle_t endpoint, const void* data, u64 size);
/* Nonblocking receive: EMPTY means the queue is empty but the peer is live. */
int ipc_recv(ipc_handle_t endpoint, void* buffer, u64 capacity,
             u32* sender_pid);
/* Kernel-only blocking variant. Returns on message, peer close or cancellation. */
int ipc_recv_wait(ipc_handle_t endpoint, void* buffer, u64 capacity,
                  u32* sender_pid);
int ipc_close(ipc_handle_t endpoint);

/*
 * Kernel-only service rendezvous. required_client_capabilities must be
 * nonzero: registration never creates an implicitly public service.
 * Rights are assigned by the service owner, never supplied by the client.
 */
int ipc_service_register(const char* name, u32 required_client_capabilities,
                         u32 server_rights, u32 client_rights,
                         ipc_service_handle_t* service_handle);
int ipc_service_unregister(ipc_service_handle_t service_handle);
int ipc_service_connect(const char* name,
                        ipc_request_handle_t* request_handle);
int ipc_service_next(ipc_service_handle_t service_handle,
                     ipc_request_handle_t* request_handle, u32* client_pid);
int ipc_service_accept(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle,
                       ipc_handle_t* server_endpoint);
int ipc_service_reject(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle);
int ipc_service_result(ipc_request_handle_t request_handle,
                       ipc_handle_t* client_endpoint);

void ipc_task_close_all(task_t* task);

/* Shared memory remains a separate kernel-only API, not IPC authority. */
#define SHM_MAX_BLOCKS      16
#define SHM_BLOCK_SIZE      4096
int shm_alloc(void);
void* shm_get(int id);
void shm_free(int id);

#ifdef IPC_HOST_TEST
/* Deterministic host-only hooks; the host runner has no real context switch. */
bool ipc_test_register_waiter(ipc_handle_t endpoint);
bool ipc_test_wake_waiters(ipc_handle_t endpoint, u32 reason);
bool ipc_test_service_generation_exhaustion(void);
bool ipc_test_request_generation_exhaustion(void);
#endif
