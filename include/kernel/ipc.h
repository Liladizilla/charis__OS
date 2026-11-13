/* ipc.h - task-owned, rights-checked IPC endpoints and service rendezvous. */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>

typedef u64 ipc_handle_t;
typedef u64 ipc_service_handle_t;
typedef u64 ipc_request_handle_t;

#define IPC_MAX_CHANNELS       32U
#define IPC_MAX_MSG_SIZE       512U
#define IPC_MAX_MESSAGES       16U
#define IPC_HANDLE_SLOT_BITS       4U
#define IPC_HANDLE_SLOT_MASK       ((1ULL << IPC_HANDLE_SLOT_BITS) - 1ULL)
#define IPC_HANDLE_GENERATION_MAX  ((1U << 28) - 1U)
#define IPC_HANDLE_INVALID        ((ipc_handle_t)0)

#define IPC_MAX_SERVICES            16U
#define IPC_SERVICE_NAME_MAX        32U
#define IPC_SERVICE_HANDLE_SLOT_BITS 4U
#define IPC_SERVICE_HANDLE_SLOT_MASK ((1ULL << IPC_SERVICE_HANDLE_SLOT_BITS) - 1ULL)
#define IPC_SERVICE_GENERATION_MAX   ((1U << 28) - 1U)
#define IPC_MAX_CONNECTIONS          32U
#define IPC_REQUEST_HANDLE_SLOT_BITS 5U
#define IPC_REQUEST_HANDLE_SLOT_MASK ((1ULL << IPC_REQUEST_HANDLE_SLOT_BITS) - 1ULL)
#define IPC_REQUEST_GENERATION_MAX   ((1U << 27) - 1U)
#define IPC_MAX_PENDING_PER_SERVICE  16U

#define IPC_RIGHT_SEND       (1U << 0)
#define IPC_RIGHT_RECEIVE    (1U << 1)
/* Required in addition to SEND/RECEIVE for file-descriptor attachments. */
#define IPC_RIGHT_TRANSFER   (1U << 2)
#define IPC_RIGHT_MASK       (IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE | IPC_RIGHT_TRANSFER)

#define IPC_STATUS_OK             0
#define IPC_STATUS_ERROR         -1
#define IPC_STATUS_PENDING        1
#define IPC_STATUS_EMPTY         -2
#define IPC_STATUS_FULL          -3
#define IPC_STATUS_BUFFER_SMALL  -4
#define IPC_STATUS_PEER_CLOSED   -5
#define IPC_STATUS_CANCELLED     -6
#define IPC_STATUS_ACCESS_DENIED -7
#define IPC_STATUS_SERVICE_NOT_FOUND -8
#define IPC_STATUS_SERVICE_EXISTS -9
#define IPC_STATUS_SERVICE_CLOSED -10
#define IPC_STATUS_REQUEST_REJECTED -11
#define IPC_STATUS_REQUEST_INVALID -12
#define IPC_STATUS_SERVICE_FULL -13
#define IPC_STATUS_ATTACHMENT_PENDING -14
#define IPC_STATUS_DESCRIPTOR_TABLE_FULL -15
#define IPC_STATUS_ATTACHMENT_FAILED -16

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
/* Kernel-only copy-transfer of one already-open descriptor. The source must
 * grant every requested right and the sender endpoint needs SEND|TRANSFER. */
int ipc_send_with_fd(ipc_handle_t endpoint, const void* data, u64 size,
                     fd_handle_t source_descriptor, u32 requested_rights);
int ipc_recv(ipc_handle_t endpoint, void* buffer, u64 capacity,
             u32* sender_pid);
int ipc_recv_wait(ipc_handle_t endpoint, void* buffer, u64 capacity,
                  u32* sender_pid);
/* Requires one output slot for the received task-local descriptor. The
 * receiver endpoint must have RECEIVE|TRANSFER for messages carrying a file. */
int ipc_recv_with_fd(ipc_handle_t endpoint, void* buffer, u64 capacity,
                     u32* sender_pid, fd_handle_t* received_descriptor);
int ipc_recv_with_fd_wait(ipc_handle_t endpoint, void* buffer, u64 capacity,
                          u32* sender_pid,
                          fd_handle_t* received_descriptor);
int ipc_close(ipc_handle_t endpoint);
void ipc_task_close_all(task_t* task);

/* Authenticated kernel-only service namespace.
 * Register/unregister require CAP_IPC_SERVICE. Connect identity is derived from
 * scheduler_current(); no caller-supplied PID or global channel ID is accepted.
 */
int ipc_service_register(const char* name, u32 required_client_capabilities,
                         ipc_service_handle_t* service);
int ipc_service_unregister(ipc_service_handle_t service);
int ipc_service_connect_begin(const char* name, u32 requested_client_rights,
                              ipc_request_handle_t* request);
int ipc_service_connect_wait(ipc_request_handle_t request,
                             ipc_handle_t* client_endpoint);
int ipc_service_connect(const char* name, u32 requested_client_rights,
                        ipc_handle_t* client_endpoint);
int ipc_service_accept_wait(ipc_service_handle_t service,
                            ipc_request_handle_t* request, u32* client_pid);
int ipc_service_accept(ipc_service_handle_t service, ipc_request_handle_t request,
                       u32 granted_client_rights, u32 server_rights,
                       ipc_handle_t* server_endpoint);
int ipc_service_reject(ipc_service_handle_t service, ipc_request_handle_t request);

/* Shared memory remains a separate kernel-only API, not IPC authority. */
#define SHM_MAX_BLOCKS      16
#define SHM_BLOCK_SIZE      4096
int shm_alloc(void);
void* shm_get(int id);
void shm_free(int id);

#ifdef IPC_HOST_TEST
bool ipc_test_irq_disabled(void);
bool ipc_test_register_waiter(ipc_handle_t endpoint);
bool ipc_test_wake_waiters(ipc_handle_t endpoint, u32 reason);
bool ipc_test_service_register_waiter(ipc_service_handle_t service);
bool ipc_test_service_dequeue(ipc_service_handle_t service,
                              ipc_request_handle_t* request, u32* client_pid);
bool ipc_test_service_generation_exhaustion(void);
bool ipc_test_request_generation_exhaustion(void);
#endif
