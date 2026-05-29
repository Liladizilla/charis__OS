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
            ipc_channels[i].in_use = true;
            return i;
        }
    }
    return -1;
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
    
    return 0;
}

int ipc_recv(int channel, void* buf, u64 max_size) {
    if (channel < 0 || channel >= IPC_MAX_CHANNELS) return -1;
    if (!ipc_channels[channel].in_use) return -1;
    if (!buf) return -1;
    
    ipc_channel_t* ch = &ipc_channels[channel];
    if (ch->count == 0) return -1; // Empty
    
    ipc_message_t* msg = &ch->messages[ch->tail];
    u64 copy_size = (msg->size < max_size) ? msg->size : max_size;
    
    u8* dst = (u8*)buf;
    for (u64 i = 0; i < copy_size; i++) {
        dst[i] = msg->data[i];
    }
    
    ch->tail = (ch->tail + 1) % IPC_MAX_MESSAGES;
    ch->count--;
    
    return copy_size;
}

void ipc_close(int channel) {
    if (channel >= 0 && channel < IPC_MAX_CHANNELS) {
        ipc_channels[channel].in_use = false;
        ipc_channels[channel].head = 0;
        ipc_channels[channel].tail = 0;
        ipc_channels[channel].count = 0;
    }
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