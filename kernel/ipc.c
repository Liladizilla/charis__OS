#include <kernel/ipc.h>
#include <kernel/scheduler.h>
#include <kernel/string.h>
#include <kernel/security.h>

typedef struct ipc_channel ipc_channel_t;
typedef struct {
    u32 sender_pid;
    u32 size;
    u8 data[IPC_MAX_MSG_SIZE];
} ipc_message_t;
typedef struct {
    ipc_message_t messages[IPC_MAX_MESSAGES];
    u32 head, tail, count;
} ipc_queue_t;

struct ipc_endpoint {
    ipc_channel_t* channel;
    task_t* owner;
    task_t* peer;
    u32 owner_pid, peer_pid, rights, generation;
    u8 slot, side;
    bool open;
    task_t* waiters;
};
struct ipc_channel {
    bool in_use;
    u32 endpoint_refs;
    struct ipc_endpoint endpoints[2];
    ipc_queue_t inbound[2];
};
struct ipc_service {
    bool in_use;
    u32 generation;
    task_t* owner;
    u32 owner_pid;
    u32 required_client_capabilities;
    char name[IPC_SERVICE_NAME_MAX];
    struct ipc_connection_request* pending_head;
    struct ipc_connection_request* pending_tail;
    u32 pending_count;
    task_t* accept_waiters;
};

struct ipc_connection_request {
    bool in_use;
    u32 generation;
    u32 state;
    struct ipc_service* service;
    task_t* client;
    u32 client_pid;
    u32 requested_client_rights;
    task_t* server;
    u32 server_pid;
    struct ipc_connection_request* next;
};

#define IPC_REQUEST_STATE_FREE      0U
#define IPC_REQUEST_STATE_PENDING   1U
#define IPC_REQUEST_STATE_DELIVERED 2U

static ipc_channel_t ipc_channels[IPC_MAX_CHANNELS];
static struct ipc_service ipc_services[IPC_MAX_SERVICES];
static struct ipc_connection_request ipc_requests[IPC_MAX_CONNECTIONS];
static u8 shm_blocks[SHM_MAX_BLOCKS][SHM_BLOCK_SIZE];
static bool shm_used[SHM_MAX_BLOCKS];

#ifdef IPC_HOST_TEST
static u64 ipc_irq_save(void) { return 0; }
static void ipc_irq_restore(u64 flags) { (void)flags; }
#else
/* Local IRQ exclusion only. The current kernel is single-CPU, not SMP-safe. */
static u64 ipc_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void ipc_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}
#endif

static u64 ipc_make_service_handle(u32 pid, u32 generation, u32 slot) {
    return ((u64)pid << 32) |
           ((u64)generation << IPC_SERVICE_HANDLE_SLOT_BITS) | slot;
}

static u64 ipc_make_request_handle(u32 pid, u32 generation, u32 slot) {
    return ((u64)pid << 32) |
           ((u64)generation << IPC_REQUEST_HANDLE_SLOT_BITS) | slot;
}

static bool ipc_copy_service_name(const char* input,
                                 char output[IPC_SERVICE_NAME_MAX]) {
    if (!input || !output) return false;
    u32 length = 0;
    for (; length < IPC_SERVICE_NAME_MAX - 1; length++) {
        char ch = input[length];
        if (!ch) break;
        bool valid = (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') ||
                     ch == '.' || ch == '-' || ch == '_';
        if (!valid) return false;
        output[length] = ch;
    }
    if (!length || (length >= IPC_SERVICE_NAME_MAX - 1 && input[length] != '\0'))
        return false;
    output[length] = '\0';
    return true;
}

static int ipc_find_service_slot_locked(void) {
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        if (!ipc_services[i].in_use && ipc_services[i].generation &&
            ipc_services[i].generation < IPC_SERVICE_GENERATION_MAX)
            return (int)i;
    }
    return -1;
}

static int ipc_find_request_slot_locked(void) {
    for (u32 i = 0; i < IPC_MAX_CONNECTIONS; i++) {
        if (!ipc_requests[i].in_use && ipc_requests[i].generation &&
            ipc_requests[i].generation < IPC_REQUEST_GENERATION_MAX)
            return (int)i;
    }
    return -1;
}

static struct ipc_service* ipc_resolve_service_locked(task_t* owner,
                                                       ipc_service_handle_t handle) {
    if (!owner || !owner->pid || !handle ||
        (u32)(handle >> 32) != owner->pid) return NULL;
    u32 slot = (u32)(handle & IPC_SERVICE_HANDLE_SLOT_MASK);
    u32 generation = (u32)((handle >> IPC_SERVICE_HANDLE_SLOT_BITS) &
                           IPC_SERVICE_GENERATION_MAX);
    if (slot >= IPC_MAX_SERVICES) return NULL;
    struct ipc_service* service = &ipc_services[slot];
    if (!service->in_use || service->generation != generation ||
        service->owner != owner || service->owner_pid != owner->pid)
        return NULL;
    return service;
}

static struct ipc_connection_request* ipc_resolve_request_locked(
    task_t* owner, ipc_request_handle_t handle, bool server_view) {
    if (!owner || !owner->pid || !handle ||
        (u32)(handle >> 32) != owner->pid) return NULL;
    u32 slot = (u32)(handle & IPC_REQUEST_HANDLE_SLOT_MASK);
    u32 generation = (u32)((handle >> IPC_REQUEST_HANDLE_SLOT_BITS) &
                           IPC_REQUEST_GENERATION_MAX);
    if (slot >= IPC_MAX_CONNECTIONS) return NULL;
    struct ipc_connection_request* request = &ipc_requests[slot];
    if (!request->in_use || request->generation != generation) return NULL;
    if (server_view) {
        if (request->state != IPC_REQUEST_STATE_DELIVERED ||
            request->server != owner || request->server_pid != owner->pid)
            return NULL;
    } else if (request->client != owner || request->client_pid != owner->pid) {
        return NULL;
    }
    return request;
}

static void ipc_service_wake_one_accept_waiter_locked(
    struct ipc_service* service) {
    if (!service || !service->accept_waiters) return;
    task_t* waiter = service->accept_waiters;
    service->accept_waiters = waiter->ipc_wait_next;
    waiter->ipc_wait_next = NULL;
    waiter->ipc_service_wait = NULL;
    waiter->ipc_service_wait_status = IPC_STATUS_OK;
    waiter->waiting_event = 0;
    waiter->event_data = 0;
    task_unblock(waiter);
}

static bool ipc_service_waiter_unlink_locked(task_t* waiter,
                                             struct ipc_service* service) {
    if (!waiter || !service) return false;
    task_t** cursor = &service->accept_waiters;
    while (*cursor && *cursor != waiter) cursor = &(*cursor)->ipc_wait_next;
    if (*cursor != waiter) return false;
    *cursor = waiter->ipc_wait_next;
    waiter->ipc_wait_next = NULL;
    waiter->ipc_service_wait = NULL;
    return true;
}

static bool ipc_request_unlink_pending_locked(
    struct ipc_connection_request* request) {
    if (!request || request->state != IPC_REQUEST_STATE_PENDING ||
        !request->service) return false;
    struct ipc_service* service = request->service;
    struct ipc_connection_request** cursor = &service->pending_head;
    struct ipc_connection_request* previous = NULL;
    while (*cursor && *cursor != request) {
        previous = *cursor;
        cursor = &(*cursor)->next;
    }
    if (*cursor != request) return false;
    *cursor = request->next;
    if (service->pending_tail == request) service->pending_tail = previous;
    request->next = NULL;
    if (service->pending_count) service->pending_count--;
    return true;
}

static void ipc_request_release_locked(struct ipc_connection_request* request) {
    if (!request || !request->in_use) return;
    if (request->generation < IPC_REQUEST_GENERATION_MAX)
        request->generation++;
    request->in_use = false;
    request->state = IPC_REQUEST_STATE_FREE;
    request->service = NULL;
    request->client = NULL;
    request->client_pid = 0;
    request->requested_client_rights = 0;
    request->server = NULL;
    request->server_pid = 0;
    request->next = NULL;
}

static void ipc_request_complete_client_locked(
    struct ipc_connection_request* request, int status,
    ipc_handle_t client_endpoint) {
    if (!request) return;
    task_t* client = request->client;
    if (client && client->pid == request->client_pid &&
        client->ipc_connect_request == request) {
        client->ipc_connect_request = NULL;
        client->ipc_connect_result =
            status == IPC_STATUS_OK ? client_endpoint : IPC_HANDLE_INVALID;
        client->ipc_connect_status = status;
        if (client->waiting_event == TASK_WAITING_IPC_CONNECT) {
            client->waiting_event = 0;
            client->event_data = 0;
            task_unblock(client);
        }
    }
    ipc_request_release_locked(request);
}

static void ipc_service_close_locked(struct ipc_service* service, int reason) {
    if (!service || !service->in_use) return;
    while (service->accept_waiters) {
        task_t* waiter = service->accept_waiters;
        service->accept_waiters = waiter->ipc_wait_next;
        waiter->ipc_wait_next = NULL;
        waiter->ipc_service_wait = NULL;
        waiter->ipc_service_wait_status = reason;
        waiter->waiting_event = 0;
        waiter->event_data = 0;
        task_unblock(waiter);
    }
    for (u32 i = 0; i < IPC_MAX_CONNECTIONS; i++) {
        struct ipc_connection_request* request = &ipc_requests[i];
        if (request->in_use && request->service == service) {
            (void)ipc_request_unlink_pending_locked(request);
            ipc_request_complete_client_locked(request, reason,
                                               IPC_HANDLE_INVALID);
        }
    }
    service->in_use = false;
    service->owner = NULL;
    service->owner_pid = 0;
    service->required_client_capabilities = 0;
    service->name[0] = '\0';
    service->pending_head = NULL;
    service->pending_tail = NULL;
    service->pending_count = 0;
    if (service->generation < IPC_SERVICE_GENERATION_MAX)
        service->generation++;
}

static bool ipc_service_pop_request_locked(
    struct ipc_service* service, task_t* server,
    ipc_request_handle_t* handle, u32* client_pid) {
    if (!service || !server || !service->pending_head || !handle) return false;
    struct ipc_connection_request* request = service->pending_head;
    service->pending_head = request->next;
    if (!service->pending_head) service->pending_tail = NULL;
    request->next = NULL;
    if (service->pending_count) service->pending_count--;
    request->state = IPC_REQUEST_STATE_DELIVERED;
    request->server = server;
    request->server_pid = server->pid;
    *handle = ipc_make_request_handle(server->pid, request->generation,
                                      (u32)(request - ipc_requests));
    if (client_pid) *client_pid = request->client_pid;
    return true;
}

static void ipc_service_cancel_client_request_locked(task_t* client) {
    if (!client) return;
    struct ipc_connection_request* request = client->ipc_connect_request;
    if (request && request->in_use && request->client == client) {
        (void)ipc_request_unlink_pending_locked(request);
        ipc_request_release_locked(request);
    }
    client->ipc_connect_request = NULL;
    client->ipc_connect_request_id = 0;
    client->ipc_connect_result = IPC_HANDLE_INVALID;
    client->ipc_connect_status = IPC_STATUS_CANCELLED;
    if (client->waiting_event == TASK_WAITING_IPC_CONNECT) {
        client->waiting_event = 0;
        client->event_data = 0;
        task_unblock(client);
    }
}

static void ipc_service_teardown_task_locked(task_t* task) {
    if (!task) return;
    if (task->ipc_service_wait) {
        (void)ipc_service_waiter_unlink_locked(task, task->ipc_service_wait);
        task->ipc_service_wait_status = IPC_STATUS_CANCELLED;
        task->waiting_event = 0;
        task->event_data = 0;
        task_unblock(task);
    }
    ipc_service_cancel_client_request_locked(task);
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        if (ipc_services[i].in_use && ipc_services[i].owner == task)
            ipc_service_close_locked(&ipc_services[i], IPC_STATUS_SERVICE_CLOSED);
    }
}

static bool ipc_task_live_kernel(const task_t* task) {
    return task && task->pid && !task->is_user && task->scheduler_queued &&
           task->state != TASK_STATE_UNUSED &&
           task->state != TASK_STATE_ALLOCATING &&
           task->state != TASK_STATE_EXITING &&
           task->state != TASK_STATE_ZOMBIE;
}
static bool ipc_rights_valid(u32 rights) {
    return rights && !(rights & ~IPC_RIGHT_MASK);
}
static int ipc_find_task_slot(const task_t* task) {
    for (u32 i = 0; task && i < IPC_MAX_ENDPOINTS; i++) {
        u32 gen = task->ipc_endpoint_generations[i];
        if (!task->ipc_endpoints[i] && gen &&
            gen < IPC_HANDLE_GENERATION_MAX) return (int)i;
    }
    return -1;
}
static int ipc_find_channel(void) {
    for (u32 i = 0; i < IPC_MAX_CHANNELS; i++)
        if (!ipc_channels[i].in_use) return (int)i;
    return -1;
}
static ipc_handle_t ipc_make_handle(u32 pid, u32 gen, u32 slot) {
    return ((u64)pid << 32) | ((u64)gen << IPC_HANDLE_SLOT_BITS) | slot;
}
static struct ipc_endpoint* ipc_resolve_locked(task_t* owner,
                                                ipc_handle_t handle,
                                                u32 rights) {
    if (!owner || !owner->pid || !handle ||
        (u32)(handle >> 32) != owner->pid) return NULL;
    u32 slot = (u32)(handle & IPC_HANDLE_SLOT_MASK);
    u32 gen = (u32)((handle >> IPC_HANDLE_SLOT_BITS) &
                    IPC_HANDLE_GENERATION_MAX);
    if (slot >= IPC_MAX_ENDPOINTS ||
        owner->ipc_endpoint_generations[slot] != gen) return NULL;
    struct ipc_endpoint* ep = owner->ipc_endpoints[slot];
    if (!ep || !ep->open || ep->owner != owner ||
        ep->owner_pid != owner->pid || ep->generation != gen ||
        (rights & ~ep->rights)) return NULL;
    return ep;
}
static void ipc_queue_clear(ipc_queue_t* q) {
    q->head = q->tail = q->count = 0;
}
static bool ipc_waiter_unlink_locked(task_t* waiter,
                                         struct ipc_endpoint* endpoint) {
    if (!waiter || !endpoint) return false;
    task_t** cursor = &endpoint->waiters;
    while (*cursor && *cursor != waiter) cursor = &(*cursor)->ipc_wait_next;
    if (*cursor != waiter) return false;
    *cursor = waiter->ipc_wait_next;
    waiter->ipc_wait_next = NULL;
    waiter->ipc_wait_endpoint = NULL;
    return true;
}

/* Callers hold local IRQ exclusion. Each wake unlinks before task-slot or
 * endpoint storage can be reused; the wake path performs no allocation. */
static void ipc_wake_waiters_locked(struct ipc_endpoint* endpoint, u32 reason) {
    if (!endpoint) return;
    while (endpoint->waiters) {
        task_t* waiter = endpoint->waiters;
        endpoint->waiters = waiter->ipc_wait_next;
        waiter->ipc_wait_next = NULL;
        waiter->ipc_wait_endpoint = NULL;
        waiter->ipc_wait_status = (s32)reason;
        waiter->waiting_event = 0;
        waiter->event_data = 0;
        task_unblock(waiter);
    }
}

static void ipc_cancel_waiter_locked(task_t* waiter, u32 reason) {
    if (!waiter) return;
    if (waiter->ipc_wait_endpoint)
        (void)ipc_waiter_unlink_locked(waiter, waiter->ipc_wait_endpoint);
    waiter->ipc_wait_next = NULL;
    waiter->ipc_wait_endpoint = NULL;
    waiter->ipc_wait_status = (s32)reason;
    waiter->waiting_event = 0;
    waiter->event_data = 0;
    task_unblock(waiter);
}

static bool ipc_waiter_register_locked(struct ipc_endpoint* endpoint,
                                       task_t* waiter) {
    if (!endpoint || !waiter || !endpoint->open ||
        waiter->ipc_wait_endpoint || waiter->ipc_wait_next ||
        waiter->waiting_event != 0) return false;
    waiter->ipc_wait_endpoint = endpoint;
    waiter->ipc_wait_next = endpoint->waiters;
    endpoint->waiters = waiter;
    waiter->ipc_wait_status = IPC_WAIT_STATUS_PENDING;
    waiter->waiting_event = TASK_WAITING_IPC;
    waiter->event_data = 0;
    waiter->state = TASK_STATE_BLOCKED;
    return true;
}

static void ipc_close_slot_locked(task_t* owner, u32 slot) {
    if (!owner || slot >= IPC_MAX_ENDPOINTS) return;
    struct ipc_endpoint* ep = owner->ipc_endpoints[slot];
    if (!ep) return;
    ipc_channel_t* ch = ep->channel;
    u32 side = ep->side;
    struct ipc_endpoint* peer = ch ? &ch->endpoints[1U - side] : NULL;
    ep->open = false;
    ipc_wake_waiters_locked(ep, IPC_WAIT_STATUS_CANCELLED);
    if (peer) ipc_wake_waiters_locked(peer, IPC_WAIT_STATUS_PEER_CLOSED);

    owner->ipc_endpoints[slot] = NULL;
    if (owner->ipc_endpoint_generations[slot] < IPC_HANDLE_GENERATION_MAX)
        owner->ipc_endpoint_generations[slot]++;
    ep->channel = NULL;
    ep->owner = NULL;
    ep->peer = NULL;
    ep->owner_pid = ep->peer_pid = ep->rights = 0;
    if (ch && ch->in_use) {
        ipc_queue_clear(&ch->inbound[side]);
        if (ch->endpoint_refs) ch->endpoint_refs--;
        if (!ch->endpoint_refs) {
            /* Payload bytes are unreachable once both queue counts are zero.
             * Avoid clearing the full ~16 KiB channel while interrupts are off. */
            ch->in_use = false;
        }
    }
}

void ipc_init(void) {
    for (u32 i = 0; i < IPC_MAX_CHANNELS; i++) {
        ipc_channel_t* ch = &ipc_channels[i];
        ch->in_use = false;
        ch->endpoint_refs = 0;
        for (u32 side = 0; side < 2; side++) {
            kmemset(&ch->endpoints[side], 0, sizeof(ch->endpoints[side]));
            ipc_queue_clear(&ch->inbound[side]);
        }
    }
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        kmemset(&ipc_services[i], 0, sizeof(ipc_services[i]));
        ipc_services[i].generation = 1;
    }
    for (u32 i = 0; i < IPC_MAX_CONNECTIONS; i++) {
        kmemset(&ipc_requests[i], 0, sizeof(ipc_requests[i]));
        ipc_requests[i].generation = 1;
    }
    for (u32 i = 0; i < SHM_MAX_BLOCKS; i++) shm_used[i] = false;
}

bool ipc_endpoint_pair_create(task_t* a, task_t* b, u32 rights_a,
                              u32 rights_b, ipc_handle_t* handle_a,
                              ipc_handle_t* handle_b) {
    if (!handle_a || !handle_b || handle_a == handle_b) return false;
    *handle_a = *handle_b = IPC_HANDLE_INVALID;
    if (!ipc_task_live_kernel(a) || !ipc_task_live_kernel(b) || a == b ||
        !ipc_rights_valid(rights_a) || !ipc_rights_valid(rights_b)) return false;

    u64 flags = ipc_irq_save();
    /* Revalidate under local IRQ exclusion: EXITING teardown and endpoint
     * publication must never pass each other on this single CPU. */
    if (!ipc_task_live_kernel(a) || !ipc_task_live_kernel(b) || a == b) {
        ipc_irq_restore(flags);
        return false;
    }
    int slot_a = ipc_find_task_slot(a), slot_b = ipc_find_task_slot(b);
    int channel_index = ipc_find_channel();
    if (slot_a < 0 || slot_b < 0 || channel_index < 0) {
        ipc_irq_restore(flags);
        return false;
    }
    u32 gen_a = a->ipc_endpoint_generations[slot_a];
    u32 gen_b = b->ipc_endpoint_generations[slot_b];
    ipc_channel_t* ch = &ipc_channels[channel_index];
    ch->in_use = true;
    ch->endpoint_refs = 2;
    for (u32 side = 0; side < 2; side++) {
        ipc_queue_clear(&ch->inbound[side]);
        kmemset(&ch->endpoints[side], 0, sizeof(ch->endpoints[side]));
    }
    ch->endpoints[0].channel = ch;
    ch->endpoints[0].owner = a;
    ch->endpoints[0].peer = b;
    ch->endpoints[0].owner_pid = a->pid;
    ch->endpoints[0].peer_pid = b->pid;
    ch->endpoints[0].rights = rights_a;
    ch->endpoints[0].generation = gen_a;
    ch->endpoints[0].slot = (u8)slot_a;
    ch->endpoints[0].side = 0;
    ch->endpoints[0].open = true;

    ch->endpoints[1].channel = ch;
    ch->endpoints[1].owner = b;
    ch->endpoints[1].peer = a;
    ch->endpoints[1].owner_pid = b->pid;
    ch->endpoints[1].peer_pid = a->pid;
    ch->endpoints[1].rights = rights_b;
    ch->endpoints[1].generation = gen_b;
    ch->endpoints[1].slot = (u8)slot_b;
    ch->endpoints[1].side = 1;
    ch->endpoints[1].open = true;
    a->ipc_endpoints[slot_a] = &ch->endpoints[0];
    b->ipc_endpoints[slot_b] = &ch->endpoints[1];
    *handle_a = ipc_make_handle(a->pid, gen_a, (u32)slot_a);
    *handle_b = ipc_make_handle(b->pid, gen_b, (u32)slot_b);
    ipc_irq_restore(flags);
    return true;
}

int ipc_send(ipc_handle_t handle, const void* data, u64 size) {
    if (size > IPC_MAX_MSG_SIZE || (size && !data)) return IPC_STATUS_ERROR;
    task_t* sender = scheduler_current();
    if (!sender) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    struct ipc_endpoint* ep = ipc_resolve_locked(sender, handle, IPC_RIGHT_SEND);
    if (!ep) { ipc_irq_restore(flags); return IPC_STATUS_ERROR; }

    ipc_channel_t* ch = ep->channel;
    u32 peer_side = 1U - ep->side;
    struct ipc_endpoint* peer = &ch->endpoints[peer_side];
    if (!ch->in_use || !peer->open || !peer->owner ||
        peer->owner != ep->peer || peer->owner->pid != peer->owner_pid ||
        peer->owner_pid != ep->peer_pid || !ipc_task_live_kernel(peer->owner)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_PEER_CLOSED;
    }
    ipc_queue_t* q = &ch->inbound[peer_side];
    if (q->count >= IPC_MAX_MESSAGES) {
        ipc_irq_restore(flags);
        return IPC_STATUS_FULL;
    }
    ipc_message_t* msg = &q->messages[q->head];
    msg->sender_pid = sender->pid;
    msg->size = (u32)size;
    const u8* src = (const u8*)data;
    for (u32 i = 0; i < (u32)size; i++) msg->data[i] = src[i];
    q->head = (q->head + 1U) % IPC_MAX_MESSAGES;
    q->count++;
    ipc_wake_waiters_locked(&ch->endpoints[peer_side], IPC_WAIT_STATUS_MESSAGE);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_recv(ipc_handle_t handle, void* buffer, u64 capacity, u32* sender_pid) {
    if (capacity && !buffer) return IPC_STATUS_ERROR;
    task_t* receiver = scheduler_current();
    if (!receiver) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    struct ipc_endpoint* ep = ipc_resolve_locked(receiver, handle, IPC_RIGHT_RECEIVE);
    if (!ep) { ipc_irq_restore(flags); return IPC_STATUS_ERROR; }
    ipc_queue_t* q = &ep->channel->inbound[ep->side];
    if (!q->count) {
        struct ipc_endpoint* peer = &ep->channel->endpoints[1U - ep->side];
        bool peer_live = peer->open && peer->owner == ep->peer &&
                         peer->owner_pid == ep->peer_pid &&
                         ipc_task_live_kernel(peer->owner);
        ipc_irq_restore(flags);
        return peer_live ? IPC_STATUS_EMPTY : IPC_STATUS_PEER_CLOSED;
    }
    ipc_message_t* msg = &q->messages[q->tail];
    if ((u64)msg->size > capacity) {
        ipc_irq_restore(flags);
        return IPC_STATUS_BUFFER_SMALL;
    }
    if (sender_pid) *sender_pid = msg->sender_pid;
    for (u32 i = 0; i < msg->size; i++) ((u8*)buffer)[i] = msg->data[i];
    int received = (int)msg->size;
    q->tail = (q->tail + 1U) % IPC_MAX_MESSAGES;
    q->count--;
    ipc_irq_restore(flags);
    return received;
}

/* The empty-queue check, waiter insertion and BLOCKED publication run
 * with local interrupts disabled. A send/close cannot be lost between them. */
int ipc_recv_wait(ipc_handle_t handle, void* buffer, u64 capacity,
                  u32* sender_pid) {
    if (capacity && !buffer) return IPC_STATUS_ERROR;
    task_t* receiver = scheduler_current();
    if (!receiver) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();

    for (;;) {
        struct ipc_endpoint* ep =
            ipc_resolve_locked(receiver, handle, IPC_RIGHT_RECEIVE);
        if (!ep) {
            bool cancelled =
                receiver->ipc_wait_status == IPC_WAIT_STATUS_CANCELLED;
            receiver->ipc_wait_status = IPC_WAIT_STATUS_IDLE;
            ipc_irq_restore(flags);
            return cancelled ? IPC_STATUS_CANCELLED : IPC_STATUS_ERROR;
        }

        ipc_queue_t* queue = &ep->channel->inbound[ep->side];
        if (queue->count) {
            ipc_message_t* message = &queue->messages[queue->tail];
            if ((u64)message->size > capacity) {
                ipc_irq_restore(flags);
                return IPC_STATUS_BUFFER_SMALL;
            }
            if (sender_pid) *sender_pid = message->sender_pid;
            for (u32 i = 0; i < message->size; i++)
                ((u8*)buffer)[i] = message->data[i];
            int received = (int)message->size;
            queue->tail = (queue->tail + 1U) % IPC_MAX_MESSAGES;
            queue->count--;
            receiver->ipc_wait_status = IPC_WAIT_STATUS_IDLE;
            ipc_irq_restore(flags);
            return received;
        }

        struct ipc_endpoint* peer = &ep->channel->endpoints[1U - ep->side];
        bool peer_live = peer->open && peer->owner == ep->peer &&
                         peer->owner_pid == ep->peer_pid &&
                         ipc_task_live_kernel(peer->owner);
        if (!peer_live) {
            receiver->ipc_wait_status = IPC_WAIT_STATUS_IDLE;
            ipc_irq_restore(flags);
            return IPC_STATUS_PEER_CLOSED;
        }

        /* Generic/spurious wakeups may leave the registration in place.
         * Re-block without linking the same task a second time. */
        if (receiver->ipc_wait_endpoint == ep &&
            receiver->waiting_event == TASK_WAITING_IPC) {
            receiver->state = TASK_STATE_BLOCKED;
        } else if (!ipc_waiter_register_locked(ep, receiver)) {
            ipc_irq_restore(flags);
            return IPC_STATUS_ERROR;
        }

        scheduler_yield();
        while (receiver->state == TASK_STATE_BLOCKED &&
               receiver->waiting_event == TASK_WAITING_IPC) {
            asm volatile("sti; hlt; cli" : : : "memory");
        }
        /* The resumed context has IF clear. Recheck message and peer state. */
    }
}

#ifdef IPC_HOST_TEST
bool ipc_test_register_waiter(ipc_handle_t handle) {
    task_t* waiter = scheduler_current();
    if (!waiter) return false;
    u64 flags = ipc_irq_save();
    struct ipc_endpoint* ep =
        ipc_resolve_locked(waiter, handle, IPC_RIGHT_RECEIVE);
    if (!ep || ep->channel->inbound[ep->side].count ||
        !ep->open || !ipc_task_live_kernel(ep->peer)) {
        ipc_irq_restore(flags);
        return false;
    }
    bool registered = ipc_waiter_register_locked(ep, waiter);
    ipc_irq_restore(flags);
    return registered;
}

bool ipc_test_wake_waiters(ipc_handle_t handle, u32 reason) {
    task_t* caller = scheduler_current();
    if (!caller) return false;
    u64 flags = ipc_irq_save();
    struct ipc_endpoint* ep =
        ipc_resolve_locked(caller, handle, IPC_RIGHT_RECEIVE);
    if (!ep || !ep->waiters) {
        ipc_irq_restore(flags);
        return false;
    }
    ipc_wake_waiters_locked(ep, reason);
    ipc_irq_restore(flags);
    return true;
}
#endif

int ipc_close(ipc_handle_t handle) {
    task_t* owner = scheduler_current();
    if (!owner) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    struct ipc_endpoint* ep = ipc_resolve_locked(owner, handle, 0);
    if (!ep) { ipc_irq_restore(flags); return IPC_STATUS_ERROR; }
    ipc_close_slot_locked(owner, ep->slot);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

void ipc_task_close_all(task_t* task) {
    if (!task) return;
    u64 flags = ipc_irq_save();
    /* Revoke service registration and all pending client/server rendezvous
     * before the TCB or any task-owned endpoint can be recycled. */
    ipc_service_teardown_task_locked(task);

    /* Unlink this TCB from a peer's endpoint waiter list before task reuse. */
    if (task->ipc_wait_endpoint || task->ipc_wait_next)
        ipc_cancel_waiter_locked(task, IPC_WAIT_STATUS_CANCELLED);
    else if (task->waiting_event == TASK_WAITING_IPC) {
        task->waiting_event = 0;
        task->event_data = 0;
        task->ipc_wait_status = IPC_WAIT_STATUS_CANCELLED;
        task_unblock(task);
    }
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        ipc_close_slot_locked(task, i);
    ipc_irq_restore(flags);
}

int ipc_service_register(const char* name,
                         u32 required_client_capabilities,
                         ipc_service_handle_t* handle) {
    task_t* owner = scheduler_current();
    if (!handle) return IPC_STATUS_ERROR;
    *handle = 0;
    if (!ipc_task_live_kernel(owner) ||
        (required_client_capabilities & ~CAP_ALL)) return IPC_STATUS_ERROR;
    if (!security_check_capability(owner, CAP_IPC_SERVICE))
        return IPC_STATUS_ACCESS_DENIED;

    char canonical[IPC_SERVICE_NAME_MAX];
    if (!ipc_copy_service_name(name, canonical)) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        struct ipc_service* existing = &ipc_services[i];
        if (existing->in_use && kstrcmp(existing->name, canonical) == 0) {
            ipc_irq_restore(flags);
            return IPC_STATUS_SERVICE_EXISTS;
        }
    }
    int slot = ipc_find_service_slot_locked();
    if (slot < 0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_FULL;
    }

    struct ipc_service* service = &ipc_services[slot];
    u32 generation = service->generation;
    kmemset(service, 0, sizeof(*service));
    service->generation = generation;
    service->in_use = true;
    service->owner = owner;
    service->owner_pid = owner->pid;
    service->required_client_capabilities = required_client_capabilities;
    kstrncpy(service->name, canonical, sizeof(service->name));
    *handle = ipc_make_service_handle(owner->pid, generation, (u32)slot);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_unregister(ipc_service_handle_t handle) {
    task_t* owner = scheduler_current();
    if (!ipc_task_live_kernel(owner)) return IPC_STATUS_ERROR;
    if (!security_check_capability(owner, CAP_IPC_SERVICE))
        return IPC_STATUS_ACCESS_DENIED;
    u64 flags = ipc_irq_save();
    struct ipc_service* service = ipc_resolve_service_locked(owner, handle);
    if (!service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    ipc_service_close_locked(service, IPC_STATUS_SERVICE_CLOSED);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_connect_begin(const char* name, u32 requested_client_rights,
                              ipc_request_handle_t* handle) {
    task_t* client = scheduler_current();
    if (!handle) return IPC_STATUS_ERROR;
    *handle = 0;
    if (!ipc_task_live_kernel(client) ||
        !ipc_rights_valid(requested_client_rights) ||
        client->ipc_connect_request || client->ipc_connect_request_id ||
        client->ipc_service_wait || client->ipc_wait_endpoint ||
        client->waiting_event != 0) return IPC_STATUS_ERROR;

    char canonical[IPC_SERVICE_NAME_MAX];
    if (!ipc_copy_service_name(name, canonical)) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    struct ipc_service* service = NULL;
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        struct ipc_service* candidate = &ipc_services[i];
        if (candidate->in_use && kstrcmp(candidate->name, canonical) == 0) {
            service = candidate;
            break;
        }
    }
    if (!service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_NOT_FOUND;
    }
    if (service->required_client_capabilities &&
        !security_check_capability(client, service->required_client_capabilities)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ACCESS_DENIED;
    }
    if (!ipc_task_live_kernel(service->owner)) {
        ipc_service_close_locked(service, IPC_STATUS_SERVICE_CLOSED);
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_CLOSED;
    }
    if (service->pending_count >= IPC_MAX_PENDING_PER_SERVICE) {
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_FULL;
    }
    int slot = ipc_find_request_slot_locked();
    if (slot < 0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_FULL;
    }

    struct ipc_connection_request* request = &ipc_requests[slot];
    u32 generation = request->generation;
    kmemset(request, 0, sizeof(*request));
    request->generation = generation;
    request->in_use = true;
    request->state = IPC_REQUEST_STATE_PENDING;
    request->service = service;
    request->client = client;
    request->client_pid = client->pid;
    request->requested_client_rights = requested_client_rights;

    if (service->pending_tail)
        service->pending_tail->next = request;
    else
        service->pending_head = request;
    service->pending_tail = request;
    service->pending_count++;

    ipc_request_handle_t client_handle =
        ipc_make_request_handle(client->pid, generation, (u32)slot);
    client->ipc_connect_request = request;
    client->ipc_connect_request_id = client_handle;
    client->ipc_connect_result = IPC_HANDLE_INVALID;
    client->ipc_connect_status = IPC_STATUS_PENDING;
    *handle = client_handle;

    /* Connect is event-driven: queue publication precedes waking one server
     * accept waiter. No client-side polling or supplied PID is involved. */
    ipc_service_wake_one_accept_waiter_locked(service);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_connect_wait(ipc_request_handle_t handle,
                             ipc_handle_t* client_endpoint) {
    task_t* client = scheduler_current();
    if (!client_endpoint) return IPC_STATUS_ERROR;
    *client_endpoint = IPC_HANDLE_INVALID;
    if (!client || !client->pid || !handle ||
        client->ipc_connect_request_id != handle ||
        (u32)(handle >> 32) != client->pid) return IPC_STATUS_REQUEST_INVALID;

    u64 flags = ipc_irq_save();
    for (;;) {
        if (client->ipc_connect_status != IPC_STATUS_PENDING) {
            int result = client->ipc_connect_status;
            if (result == IPC_STATUS_OK)
                *client_endpoint = client->ipc_connect_result;
            client->ipc_connect_request = NULL;
            client->ipc_connect_request_id = 0;
            client->ipc_connect_result = IPC_HANDLE_INVALID;
            client->ipc_connect_status = 0;
            ipc_irq_restore(flags);
            return result;
        }

        struct ipc_connection_request* request = client->ipc_connect_request;
        if (!request || !request->in_use || request->client != client ||
            request->client_pid != client->pid) {
            /* A stale TCB request pointer is never followed or dereferenced. */
            client->ipc_connect_request = NULL;
            client->ipc_connect_request_id = 0;
            client->ipc_connect_result = IPC_HANDLE_INVALID;
            client->ipc_connect_status = 0;
            ipc_irq_restore(flags);
            return IPC_STATUS_REQUEST_INVALID;
        }

        if (client->waiting_event == TASK_WAITING_IPC_CONNECT) {
            client->state = TASK_STATE_BLOCKED;
        } else if (client->waiting_event == 0 &&
                   !client->ipc_wait_endpoint && !client->ipc_service_wait) {
            client->waiting_event = TASK_WAITING_IPC_CONNECT;
            client->event_data = handle;
            client->state = TASK_STATE_BLOCKED;
        } else {
            ipc_irq_restore(flags);
            return IPC_STATUS_ERROR;
        }

        scheduler_yield();
        while (client->state == TASK_STATE_BLOCKED &&
               client->waiting_event == TASK_WAITING_IPC_CONNECT) {
            asm volatile("sti; hlt; cli" : : : "memory");
        }
        /* Spurious/generic wake: retain the token and recheck completion. */
    }
}

int ipc_service_connect(const char* name, u32 requested_client_rights,
                        ipc_handle_t* client_endpoint) {
    if (!client_endpoint) return IPC_STATUS_ERROR;
    *client_endpoint = IPC_HANDLE_INVALID;
    ipc_request_handle_t request = 0;
    int result = ipc_service_connect_begin(name, requested_client_rights,
                                           &request);
    if (result != IPC_STATUS_OK) return result;
    return ipc_service_connect_wait(request, client_endpoint);
}

int ipc_service_accept_wait(ipc_service_handle_t handle,
                            ipc_request_handle_t* request_handle,
                            u32* client_pid) {
    task_t* server = scheduler_current();
    if (!request_handle) return IPC_STATUS_ERROR;
    *request_handle = 0;
    if (client_pid) *client_pid = 0;
    if (!ipc_task_live_kernel(server)) return IPC_STATUS_ERROR;
    if (!security_check_capability(server, CAP_IPC_SERVICE))
        return IPC_STATUS_ACCESS_DENIED;

    u64 flags = ipc_irq_save();
    for (;;) {
        struct ipc_service* service =
            ipc_resolve_service_locked(server, handle);
        if (!service) {
            int wake_status = server->ipc_service_wait_status;
            server->ipc_service_wait_status = 0;
            ipc_irq_restore(flags);
            return (wake_status == IPC_STATUS_SERVICE_CLOSED ||
                    wake_status == IPC_STATUS_CANCELLED)
                ? wake_status : IPC_STATUS_ERROR;
        }
        if (ipc_service_pop_request_locked(service, server,
                                           request_handle, client_pid)) {
            ipc_irq_restore(flags);
            return IPC_STATUS_OK;
        }

        if (server->ipc_service_wait == service &&
            server->waiting_event == TASK_WAITING_IPC_SERVICE) {
            server->state = TASK_STATE_BLOCKED;
        } else if (!server->ipc_service_wait && !server->ipc_wait_next &&
                   !server->ipc_wait_endpoint && !server->ipc_connect_request &&
                   server->waiting_event == 0) {
            server->ipc_service_wait = service;
            server->ipc_wait_next = service->accept_waiters;
            service->accept_waiters = server;
            server->ipc_service_wait_status = IPC_STATUS_PENDING;
            server->waiting_event = TASK_WAITING_IPC_SERVICE;
            server->event_data = 0;
            server->state = TASK_STATE_BLOCKED;
        } else {
            ipc_irq_restore(flags);
            return IPC_STATUS_ERROR;
        }

        scheduler_yield();
        while (server->state == TASK_STATE_BLOCKED &&
               server->waiting_event == TASK_WAITING_IPC_SERVICE) {
            asm volatile("sti; hlt; cli" : : : "memory");
        }
        /* A request or service-close event wakes us; always re-resolve owner
         * and generation before touching the service object again. */
    }
}

int ipc_service_accept(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle,
                       u32 granted_client_rights, u32 server_rights,
                       ipc_handle_t* server_endpoint) {
    task_t* server = scheduler_current();
    if (!server_endpoint) return IPC_STATUS_ERROR;
    *server_endpoint = IPC_HANDLE_INVALID;
    if (!ipc_task_live_kernel(server)) return IPC_STATUS_ERROR;
    if (!ipc_rights_valid(granted_client_rights) ||
        !ipc_rights_valid(server_rights)) return IPC_STATUS_ERROR;
    if (!security_check_capability(server, CAP_IPC_SERVICE))
        return IPC_STATUS_ACCESS_DENIED;

    u64 flags = ipc_irq_save();
    struct ipc_service* service =
        ipc_resolve_service_locked(server, service_handle);
    struct ipc_connection_request* request =
        ipc_resolve_request_locked(server, request_handle, true);
    if (!service || !request || request->service != service ||
        (granted_client_rights & ~request->requested_client_rights) != 0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_REQUEST_INVALID;
    }
    task_t* client = request->client;
    if (!ipc_task_live_kernel(client) ||
        client->ipc_connect_request != request ||
        client->ipc_connect_request_id !=
            ipc_make_request_handle(client->pid, request->generation,
                                    (u32)(request - ipc_requests))) {
        (void)ipc_request_unlink_pending_locked(request);
        if (client && client->ipc_connect_request == request &&
            client->ipc_connect_request_id) {
            client->ipc_connect_request = NULL;
            client->ipc_connect_request_id = 0;
            client->ipc_connect_result = IPC_HANDLE_INVALID;
            client->ipc_connect_status = IPC_STATUS_REQUEST_INVALID;
            if (client->waiting_event == TASK_WAITING_IPC_CONNECT) {
                client->waiting_event = 0;
                client->event_data = 0;
                task_unblock(client);
            }
        }
        ipc_request_release_locked(request);
        ipc_irq_restore(flags);
        return IPC_STATUS_REQUEST_INVALID;
    }

    ipc_handle_t client_endpoint = IPC_HANDLE_INVALID;
    ipc_handle_t endpoint_for_server = IPC_HANDLE_INVALID;
    if (!ipc_endpoint_pair_create(client, server, granted_client_rights,
                                  server_rights, &client_endpoint,
                                  &endpoint_for_server)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_SERVICE_FULL;
    }
    ipc_request_complete_client_locked(request, IPC_STATUS_OK,
                                       client_endpoint);
    *server_endpoint = endpoint_for_server;
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_reject(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle) {
    task_t* server = scheduler_current();
    if (!ipc_task_live_kernel(server)) return IPC_STATUS_ERROR;
    if (!security_check_capability(server, CAP_IPC_SERVICE))
        return IPC_STATUS_ACCESS_DENIED;
    u64 flags = ipc_irq_save();
    struct ipc_service* service =
        ipc_resolve_service_locked(server, service_handle);
    struct ipc_connection_request* request =
        ipc_resolve_request_locked(server, request_handle, true);
    if (!service || !request || request->service != service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_REQUEST_INVALID;
    }
    ipc_request_complete_client_locked(request, IPC_STATUS_REQUEST_REJECTED,
                                       IPC_HANDLE_INVALID);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

#ifdef IPC_HOST_TEST
bool ipc_test_service_register_waiter(ipc_service_handle_t handle) {
    task_t* server = scheduler_current();
    if (!server) return false;
    u64 flags = ipc_irq_save();
    struct ipc_service* service = ipc_resolve_service_locked(server, handle);
    if (!service || service->pending_head || server->ipc_service_wait ||
        server->ipc_wait_next || server->waiting_event != 0) {
        ipc_irq_restore(flags);
        return false;
    }
    server->ipc_service_wait = service;
    server->ipc_wait_next = service->accept_waiters;
    service->accept_waiters = server;
    server->ipc_service_wait_status = IPC_STATUS_PENDING;
    server->waiting_event = TASK_WAITING_IPC_SERVICE;
    server->state = TASK_STATE_BLOCKED;
    ipc_irq_restore(flags);
    return true;
}

bool ipc_test_service_dequeue(ipc_service_handle_t handle,
                              ipc_request_handle_t* request_handle,
                              u32* client_pid) {
    task_t* server = scheduler_current();
    if (!server || !request_handle) return false;
    u64 flags = ipc_irq_save();
    struct ipc_service* service = ipc_resolve_service_locked(server, handle);
    bool result = service && ipc_service_pop_request_locked(
        service, server, request_handle, client_pid);
    ipc_irq_restore(flags);
    return result;
}
bool ipc_test_service_generation_exhaustion(void) {
    u64 flags = ipc_irq_save();
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        ipc_services[i].in_use = false;
        ipc_services[i].generation = IPC_SERVICE_GENERATION_MAX;
    }
    bool exhausted = ipc_find_service_slot_locked() < 0;
    ipc_irq_restore(flags);
    return exhausted;
}

bool ipc_test_request_generation_exhaustion(void) {
    u64 flags = ipc_irq_save();
    for (u32 i = 0; i < IPC_MAX_CONNECTIONS; i++) {
        ipc_requests[i].in_use = false;
        ipc_requests[i].generation = IPC_REQUEST_GENERATION_MAX;
    }
    bool exhausted = ipc_find_request_slot_locked() < 0;
    ipc_irq_restore(flags);
    return exhausted;
}

#endif

int shm_alloc(void) {
    u64 flags = ipc_irq_save();
    for (u32 i = 0; i < SHM_MAX_BLOCKS; i++) {
        if (!shm_used[i]) { shm_used[i] = true; ipc_irq_restore(flags); return (int)i; }
    }
    ipc_irq_restore(flags);
    return -1;
}
void* shm_get(int id) {
    if (id < 0 || id >= SHM_MAX_BLOCKS) return NULL;
    u64 flags = ipc_irq_save();
    void* ptr = shm_used[id] ? (void*)shm_blocks[id] : NULL;
    ipc_irq_restore(flags);
    return ptr;
}
void shm_free(int id) {
    if (id < 0 || id >= SHM_MAX_BLOCKS) return;
    u64 flags = ipc_irq_save();
    shm_used[id] = false;
    ipc_irq_restore(flags);
}
