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

typedef struct {
    bool in_use;
    task_t* owner;
    u32 owner_pid;
    u32 generation;
    u32 required_client_capabilities;
    u32 server_rights;
    u32 client_rights;
    char name[IPC_MAX_SERVICE_NAME];
} ipc_service_t;

typedef enum {
    IPC_REQUEST_FREE = 0,
    IPC_REQUEST_PENDING,
    IPC_REQUEST_OFFERED,
    IPC_REQUEST_ACCEPTED,
    IPC_REQUEST_REJECTED
} ipc_request_state_t;

typedef struct {
    bool in_use;
    ipc_request_state_t state;
    task_t* client;
    u32 client_pid;
    ipc_service_t* service;
    u32 service_generation;
    u32 generation;
    u64 sequence;
    ipc_handle_t client_endpoint;
    s32 result;
} ipc_connect_request_t;

static ipc_channel_t ipc_channels[IPC_MAX_CHANNELS];
static ipc_service_t ipc_services[IPC_MAX_SERVICES];
static ipc_connect_request_t ipc_connect_requests[IPC_MAX_CONNECT_REQUESTS];
static u32 ipc_service_generations[IPC_MAX_SERVICES];
static u32 ipc_request_generations[IPC_MAX_CONNECT_REQUESTS];
static u64 ipc_request_sequence;
static bool ipc_initialized;
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
static bool ipc_service_policy_valid(u32 required_capabilities) {
    return required_capabilities &&
           !(required_capabilities & ~((u32)CAP_ALL));
}
static bool ipc_service_client_authorized(task_t* client,
                                          const ipc_service_t* service) {
    return ipc_task_live_kernel(client) && service &&
           security_check_capability(client,
                                     service->required_client_capabilities);
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
static bool ipc_service_name_valid(const char* name, u32* out_length) {
    if (!name) return false;
    for (u32 i = 0; i < IPC_MAX_SERVICE_NAME; i++) {
        u8 ch = (u8)name[i];
        if (!ch) {
            if (!i) return false;
            if (out_length) *out_length = i;
            return true;
        }
        bool valid = (ch >= 'a' && ch <= 'z') ||
                     (ch >= 'A' && ch <= 'Z') ||
                     (ch >= '0' && ch <= '9') ||
                     ch == '.' || ch == '_' || ch == '-';
        if (!valid) return false;
    }
    return false;
}

static bool ipc_service_name_equal(const char* a, const char* b) {
    if (!a || !b) return false;
    for (u32 i = 0; i < IPC_MAX_SERVICE_NAME; i++) {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}

static int ipc_find_service_slot(void) {
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++)
        if (!ipc_services[i].in_use && ipc_service_generations[i] &&
            ipc_service_generations[i] < IPC_SERVICE_GENERATION_MAX)
            return (int)i;
    return -1;
}

static int ipc_find_request_slot(void) {
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++)
        if (!ipc_connect_requests[i].in_use && ipc_request_generations[i] &&
            ipc_request_generations[i] < IPC_REQUEST_GENERATION_MAX)
            return (int)i;
    return -1;
}

static ipc_service_handle_t ipc_make_service_handle(u32 pid, u32 generation,
                                                     u32 slot) {
    return ((u64)pid << 32) |
           ((u64)generation << IPC_SERVICE_SLOT_BITS) | (u64)slot;
}

static ipc_request_handle_t ipc_make_request_handle(u32 pid, u32 generation,
                                                     u32 slot) {
    return ((u64)pid << 32) |
           ((u64)generation << IPC_REQUEST_SLOT_BITS) | (u64)slot;
}

static ipc_service_t* ipc_resolve_service_locked(task_t* owner,
                                                  ipc_service_handle_t handle) {
    if (!ipc_task_live_kernel(owner) || !handle ||
        (u32)(handle >> 32) != owner->pid) return NULL;
    u32 slot = (u32)(handle & IPC_SERVICE_SLOT_MASK);
    u32 generation = (u32)((handle >> IPC_SERVICE_SLOT_BITS) &
                           IPC_SERVICE_GENERATION_MAX);
    if (slot >= IPC_MAX_SERVICES ||
        ipc_service_generations[slot] != generation) return NULL;
    ipc_service_t* service = &ipc_services[slot];
    if (!service->in_use || service->owner != owner ||
        service->owner_pid != owner->pid || service->generation != generation)
        return NULL;
    return service;
}

static ipc_connect_request_t* ipc_resolve_request_for_client_locked(
    task_t* client, ipc_request_handle_t handle) {
    if (!ipc_task_live_kernel(client) || !handle ||
        (u32)(handle >> 32) != client->pid) return NULL;
    u32 slot = (u32)(handle & IPC_REQUEST_SLOT_MASK);
    u32 generation = (u32)((handle >> IPC_REQUEST_SLOT_BITS) &
                           IPC_REQUEST_GENERATION_MAX);
    if (slot >= IPC_MAX_CONNECT_REQUESTS ||
        ipc_request_generations[slot] != generation) return NULL;
    ipc_connect_request_t* request = &ipc_connect_requests[slot];
    if (!request->in_use || request->client != client ||
        request->client_pid != client->pid ||
        request->generation != generation) return NULL;
    return request;
}

static ipc_connect_request_t* ipc_resolve_request_for_service_locked(
    ipc_service_t* service, ipc_request_handle_t handle) {
    if (!service || !service->in_use || !handle) return NULL;
    u32 client_pid = (u32)(handle >> 32);
    u32 slot = (u32)(handle & IPC_REQUEST_SLOT_MASK);
    u32 generation = (u32)((handle >> IPC_REQUEST_SLOT_BITS) &
                           IPC_REQUEST_GENERATION_MAX);
    if (!client_pid || slot >= IPC_MAX_CONNECT_REQUESTS ||
        ipc_request_generations[slot] != generation) return NULL;
    ipc_connect_request_t* request = &ipc_connect_requests[slot];
    if (!request->in_use || request->client_pid != client_pid ||
        request->generation != generation ||
        request->service != service ||
        request->service_generation != service->generation ||
        (request->state != IPC_REQUEST_PENDING &&
         request->state != IPC_REQUEST_OFFERED)) return NULL;
    return request;
}

static void ipc_request_release_locked(ipc_connect_request_t* request) {
    if (!request || !request->in_use) return;
    u32 slot = (u32)(request - ipc_connect_requests);
    if (slot >= IPC_MAX_CONNECT_REQUESTS) return;
    request->in_use = false;
    request->state = IPC_REQUEST_FREE;
    request->client = NULL;
    request->client_pid = 0;
    request->service = NULL;
    request->service_generation = 0;
    request->sequence = 0;
    request->client_endpoint = IPC_HANDLE_INVALID;
    request->result = IPC_STATUS_ERROR;
    if (ipc_request_generations[slot] < IPC_REQUEST_GENERATION_MAX)
        ipc_request_generations[slot]++;
}

static void ipc_service_revoke_locked(ipc_service_t* service) {
    if (!service || !service->in_use) return;
    u32 slot = (u32)(service - ipc_services);
    if (slot >= IPC_MAX_SERVICES) return;
    u32 generation = service->generation;
    service->in_use = false;
    service->owner = NULL;
    service->owner_pid = 0;
    service->required_client_capabilities = 0;
    service->server_rights = 0;
    service->client_rights = 0;
    service->name[0] = '\0';
    if (ipc_service_generations[slot] < IPC_SERVICE_GENERATION_MAX)
        ipc_service_generations[slot]++;

    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++) {
        ipc_connect_request_t* request = &ipc_connect_requests[i];
        if (!request->in_use || request->service != service ||
            request->service_generation != generation) continue;
        if (request->state == IPC_REQUEST_PENDING ||
            request->state == IPC_REQUEST_OFFERED) {
            request->state = IPC_REQUEST_REJECTED;
            request->result = IPC_STATUS_REJECTED;
            request->service = NULL;
            request->service_generation = 0;
        }
    }
}

static void ipc_service_close_all_locked(task_t* owner) {
    if (!owner) return;
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        ipc_service_t* service = &ipc_services[i];
        if (service->in_use && service->owner == owner &&
            service->owner_pid == owner->pid)
            ipc_service_revoke_locked(service);
    }
}

static void ipc_client_requests_close_all_locked(task_t* client) {
    if (!client) return;
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++) {
        ipc_connect_request_t* request = &ipc_connect_requests[i];
        if (request->in_use && request->client == client &&
            request->client_pid == client->pid)
            ipc_request_release_locked(request);
    }
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
    /* Generation counters are lifetime guards; a second init must not reset
     * them and make a stale service/request token valid again. */
    if (ipc_initialized) return;
    ipc_initialized = true;
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
        ipc_service_generations[i] = 1;
    }
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++) {
        kmemset(&ipc_connect_requests[i], 0, sizeof(ipc_connect_requests[i]));
        ipc_request_generations[i] = 1;
    }
    ipc_request_sequence = 0;
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

/* Registration is private to a live kernel task and requires an explicit,
 * nonzero client capability mask. */
int ipc_service_register(const char* name, u32 required_caps,
                         u32 server_rights, u32 client_rights,
                         ipc_service_handle_t* service_handle) {
    if (!service_handle) return IPC_STATUS_ERROR;
    *service_handle = 0;
    u32 name_length = 0;
    if (!ipc_service_name_valid(name, &name_length) ||
        !ipc_service_policy_valid(required_caps) ||
        !ipc_rights_valid(server_rights) || !ipc_rights_valid(client_rights))
        return IPC_STATUS_ERROR;
    task_t* owner = scheduler_current();
    if (!ipc_task_live_kernel(owner)) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    if (!ipc_task_live_kernel(owner)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        if (ipc_services[i].in_use &&
            ipc_service_name_equal(ipc_services[i].name, name)) {
            ipc_irq_restore(flags);
            return IPC_STATUS_ERROR;
        }
    }
    int slot = ipc_find_service_slot();
    if (slot < 0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_LIMIT;
    }
    ipc_service_t* service = &ipc_services[slot];
    kmemset(service, 0, sizeof(*service));
    service->in_use = true;
    service->owner = owner;
    service->owner_pid = owner->pid;
    service->generation = ipc_service_generations[slot];
    service->required_client_capabilities = required_caps;
    service->server_rights = server_rights;
    service->client_rights = client_rights;
    for (u32 i = 0; i < name_length; i++) service->name[i] = name[i];
    service->name[name_length] = '\0';
    *service_handle = ipc_make_service_handle(owner->pid, service->generation,
                                               (u32)slot);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_unregister(ipc_service_handle_t handle) {
    task_t* owner = scheduler_current();
    if (!owner) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    ipc_service_t* service = ipc_resolve_service_locked(owner, handle);
    if (!service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    ipc_service_revoke_locked(service);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_connect(const char* name, ipc_request_handle_t* request_handle) {
    if (!request_handle) return IPC_STATUS_ERROR;
    *request_handle = 0;
    if (!ipc_service_name_valid(name, NULL)) return IPC_STATUS_ERROR;
    task_t* client = scheduler_current();
    if (!ipc_task_live_kernel(client)) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    if (!ipc_task_live_kernel(client)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    ipc_service_t* service = NULL;
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        ipc_service_t* candidate = &ipc_services[i];
        if (candidate->in_use &&
            ipc_service_name_equal(candidate->name, name)) {
            service = candidate;
            break;
        }
    }
    if (!service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    if (!ipc_service_client_authorized(client, service)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_DENIED;
    }
    if (ipc_request_sequence == ~(u64)0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_LIMIT;
    }
    int slot = ipc_find_request_slot();
    if (slot < 0) {
        ipc_irq_restore(flags);
        return IPC_STATUS_LIMIT;
    }
    ipc_connect_request_t* request = &ipc_connect_requests[slot];
    kmemset(request, 0, sizeof(*request));
    request->in_use = true;
    request->state = IPC_REQUEST_PENDING;
    request->client = client;
    request->client_pid = client->pid;
    request->service = service;
    request->service_generation = service->generation;
    request->generation = ipc_request_generations[slot];
    request->sequence = ++ipc_request_sequence;
    request->result = IPC_STATUS_EMPTY;
    *request_handle = ipc_make_request_handle(client->pid,
                                               request->generation,
                                               (u32)slot);
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_next(ipc_service_handle_t service_handle,
                     ipc_request_handle_t* request_handle, u32* client_pid) {
    if (!request_handle || !client_pid) return IPC_STATUS_ERROR;
    *request_handle = 0;
    *client_pid = 0;
    task_t* server = scheduler_current();
    if (!server) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    ipc_service_t* service = ipc_resolve_service_locked(server, service_handle);
    if (!service) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    ipc_connect_request_t* oldest = NULL;
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++) {
        ipc_connect_request_t* request = &ipc_connect_requests[i];
        if (!request->in_use || request->state != IPC_REQUEST_PENDING ||
            request->service != service ||
            request->service_generation != service->generation) continue;
        if (!ipc_task_live_kernel(request->client) ||
            request->client->pid != request->client_pid) {
            /* A dead client cannot consume a terminal result. Reclaim its
             * request token instead of leaking a permanent table slot. */
            ipc_request_release_locked(request);
            continue;
        }
        if (!ipc_service_client_authorized(request->client, service)) {
            request->state = IPC_REQUEST_REJECTED;
            request->result = IPC_STATUS_DENIED;
            request->service = NULL;
            request->service_generation = 0;
            continue;
        }
        if (!oldest || request->sequence < oldest->sequence) oldest = request;
    }
    if (!oldest) {
        ipc_irq_restore(flags);
        return IPC_STATUS_EMPTY;
    }
    oldest->state = IPC_REQUEST_OFFERED;
    *request_handle = ipc_make_request_handle(oldest->client_pid,
                                               oldest->generation,
                                               (u32)(oldest - ipc_connect_requests));
    *client_pid = oldest->client_pid;
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_accept(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle,
                       ipc_handle_t* server_endpoint) {
    if (!server_endpoint) return IPC_STATUS_ERROR;
    *server_endpoint = IPC_HANDLE_INVALID;
    task_t* server = scheduler_current();
    if (!server) return IPC_STATUS_ERROR;

    u64 flags = ipc_irq_save();
    ipc_service_t* service = ipc_resolve_service_locked(server, service_handle);
    ipc_connect_request_t* request =
        ipc_resolve_request_for_service_locked(service, request_handle);
    if (!service || !request || request->state != IPC_REQUEST_OFFERED) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    task_t* client = request->client;
    if (!ipc_task_live_kernel(client) || client->pid != request->client_pid) {
        ipc_request_release_locked(request);
        ipc_irq_restore(flags);
        return IPC_STATUS_PEER_CLOSED;
    }
    if (!ipc_service_client_authorized(client, service)) {
        request->state = IPC_REQUEST_REJECTED;
        request->result = IPC_STATUS_DENIED;
        request->service = NULL;
        request->service_generation = 0;
        ipc_irq_restore(flags);
        return IPC_STATUS_DENIED;
    }

    ipc_handle_t server_handle = IPC_HANDLE_INVALID;
    ipc_handle_t client_handle = IPC_HANDLE_INVALID;
    /* Nested save/restore preserves IF=0; service and endpoint publication
     * are atomic against unregister and client teardown on this single CPU. */
    if (!ipc_endpoint_pair_create(server, client, service->server_rights,
                                  service->client_rights, &server_handle,
                                  &client_handle)) {
        ipc_irq_restore(flags);
        return IPC_STATUS_LIMIT;
    }
    request->state = IPC_REQUEST_ACCEPTED;
    request->service = NULL;
    request->service_generation = 0;
    request->client_endpoint = client_handle;
    request->result = IPC_STATUS_OK;
    *server_endpoint = server_handle;
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_reject(ipc_service_handle_t service_handle,
                       ipc_request_handle_t request_handle) {
    task_t* server = scheduler_current();
    if (!server) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    ipc_service_t* service = ipc_resolve_service_locked(server, service_handle);
    ipc_connect_request_t* request =
        ipc_resolve_request_for_service_locked(service, request_handle);
    if (!service || !request || request->state != IPC_REQUEST_OFFERED) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    request->state = IPC_REQUEST_REJECTED;
    request->result = IPC_STATUS_REJECTED;
    request->service = NULL;
    request->service_generation = 0;
    ipc_irq_restore(flags);
    return IPC_STATUS_OK;
}

int ipc_service_result(ipc_request_handle_t request_handle,
                       ipc_handle_t* client_endpoint) {
    if (!client_endpoint) return IPC_STATUS_ERROR;
    *client_endpoint = IPC_HANDLE_INVALID;
    task_t* client = scheduler_current();
    if (!client) return IPC_STATUS_ERROR;
    u64 flags = ipc_irq_save();
    ipc_connect_request_t* request =
        ipc_resolve_request_for_client_locked(client, request_handle);
    if (!request) {
        ipc_irq_restore(flags);
        return IPC_STATUS_ERROR;
    }
    if (request->state == IPC_REQUEST_PENDING ||
        request->state == IPC_REQUEST_OFFERED) {
        ipc_irq_restore(flags);
        return IPC_STATUS_EMPTY;
    }
    if (request->state == IPC_REQUEST_ACCEPTED) {
        *client_endpoint = request->client_endpoint;
        ipc_request_release_locked(request);
        ipc_irq_restore(flags);
        return IPC_STATUS_OK;
    }
    s32 result = request->result ? request->result : IPC_STATUS_REJECTED;
    ipc_request_release_locked(request);
    ipc_irq_restore(flags);
    return result;
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
    /* Revoke service registrations and discard client request tokens before
     * service/request/endpoint/TCB storage can be reused. */
    ipc_service_close_all_locked(task);
    ipc_client_requests_close_all_locked(task);
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

#ifdef IPC_HOST_TEST
bool ipc_test_service_generation_exhaustion(void) {
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++)
        if (ipc_services[i].in_use) return false;
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++)
        ipc_service_generations[i] = IPC_SERVICE_GENERATION_MAX;
    return ipc_find_service_slot() < 0;
}
bool ipc_test_request_generation_exhaustion(void) {
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++)
        if (ipc_connect_requests[i].in_use) return false;
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++)
        ipc_request_generations[i] = IPC_REQUEST_GENERATION_MAX;
    return ipc_find_request_slot() < 0;
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
