#include <kernel/ipc.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>

static ipc_channel_t ipc_channels[IPC_MAX_CHANNELS];
static char shm_blocks[SHM_MAX_BLOCKS][SHM_BLOCK_SIZE];
static bool shm_used[SHM_MAX_BLOCKS];

void ipc_init(void) {
    for (int i = 0; i < IPC_MAX_CHANNELS; i++) {
        ipc_channels[i].in_use = false;
        ipc_channels[i].head = 0;
        ipc_channels[i].tail = 0;
        ipc_channels[i].count = 0;
        ipc_channels[i].wait_head = NULL;
    }
    for (int i = 0; i < SHM_MAX_BLOCKS; i++) {
        shm_used[i] = false;
    }
    vga_puts("IPC initialized\n");
}

int ipc_create(const char* name) {
    (void)name;
    for (int i = 0; i < IPC_MAX_CHANNELS; i++) {
        if (!ipc_channels[i].in_use) {
            ipc_channel_t* ch = &ipc_channels[i];
            ch->in_use = true;
            ch->head = 0;
            ch->tail = 0;
            ch->count = 0;
            ch->wait_head = NULL;
            return i;
        }
    }
    return -1;
}

/* Remove a task from this channel's wait list before re-queuing it. A task can
 * be woken by another kernel event while its IPC wait is pending; blindly
 * inserting it again would make the singly-linked list point back to itself. */
static void ipc_remove_waiter(ipc_channel_t* ch, task_t* target) {
    if (!ch || !target) return;

    task_t* previous = NULL;
    task_t* current = ch->wait_head;
    while (current) {
        if (current == target) {
            if (previous) previous->ipc_wait_next = current->ipc_wait_next;
            else ch->wait_head = current->ipc_wait_next;
            current->ipc_wait_next = NULL;
            return;
        }
        previous = current;
        current = current->ipc_wait_next;
    }
}

int ipc_send(int channel, void* data, u64 size) {
    if (channel < 0 || channel >= IPC_MAX_CHANNELS) return -1;
    if (!ipc_channels[channel].in_use) return -1;
    if (!data) return -1;
    if (size > IPC_MAX_MSG_SIZE) size = IPC_MAX_MSG_SIZE;

    ipc_channel_t* ch = &ipc_channels[channel];
    if (ch->count >= IPC_MAX_MESSAGES) return -1; // Full

    ipc_message_t* msg = &ch->messages[ch->head];
    task_t* sender = scheduler_current();
    msg->sender_pid = sender ? sender->pid : 0;
    msg->size = size;

    u8* src = (u8*)data;
    for (u64 i = 0; i < size; i++) {
        msg->data[i] = src[i];
    }

    ch->head = (ch->head + 1) % IPC_MAX_MESSAGES;
    ch->count++;

    /* Wake one receiver for each newly queued message. Detach it before
     * making it runnable so it cannot remain linked while it is executing. */
    if (ch->wait_head) {
        task_t* waiter = ch->wait_head;
        ch->wait_head = waiter->ipc_wait_next;
        waiter->ipc_wait_next = NULL;
        scheduler_unblock_task(waiter);
    }

    return 0;
}

int ipc_recv(int channel, void* buf, u64 max_size) {
    if (channel < 0 || channel >= IPC_MAX_CHANNELS) return -1;
    if (!ipc_channels[channel].in_use) return -1;
    if (max_size == 0) return 0;
    if (!buf) return -1;

    ipc_channel_t* ch = &ipc_channels[channel];

    /* A wake-up is only a hint: another receiver may have consumed the
     * message first, or the channel may have been closed. Always re-check
     * state before touching the ring buffer. */
    while (ch->count == 0) {
        task_t* current = scheduler_current();
        if (!current) return -1;

        ipc_remove_waiter(ch, current);
        current->ipc_wait_next = ch->wait_head;
        ch->wait_head = current;
        scheduler_block_task(current);
        scheduler_yield();

        if (!ch->in_use) return -1;
    }

    ipc_message_t* msg = &ch->messages[ch->tail];
    u64 copy_size = (msg->size < max_size) ? msg->size : max_size;

    u8* dst = (u8*)buf;
    for (u64 i = 0; i < copy_size; i++) {
        dst[i] = msg->data[i];
    }

    ch->tail = (ch->tail + 1) % IPC_MAX_MESSAGES;
    ch->count--;

    return (int)copy_size;
}

void ipc_close(int channel) {
    if (channel < 0 || channel >= IPC_MAX_CHANNELS) return;

    ipc_channel_t* ch = &ipc_channels[channel];
    if (!ch->in_use) return;

    /* Closing a channel must not strand tasks forever in BLOCKED state.
     * Detach the entire wait list and wake each receiver so it can observe
     * that the channel is closed and return an error. */
    task_t* waiter = ch->wait_head;
    ch->wait_head = NULL;
    while (waiter) {
        task_t* next = waiter->ipc_wait_next;
        waiter->ipc_wait_next = NULL;
        scheduler_unblock_task(waiter);
        waiter = next;
    }

    ch->in_use = false;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
}

int shm_alloc(void) {
    for (int i = 0; i < SHM_MAX_BLOCKS; i++) {
        if (!shm_used[i]) {
            shm_used[i] = true;
            return i;
        }
    }
    return -1;
}

void* shm_get(int id) {
    if (id < 0 || id >= SHM_MAX_BLOCKS) return NULL;
    if (!shm_used[id]) return NULL;
    return shm_blocks[id];
}

void shm_free(int id) {
    if (id >= 0 && id < SHM_MAX_BLOCKS) {
        shm_used[id] = false;
    }
}
