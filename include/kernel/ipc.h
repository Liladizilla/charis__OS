/* ipc.h - Inter-process communication for CharisOS */
#pragma once
#include <kernel/types.h>

#define IPC_MAX_CHANNELS      32
#define IPC_MAX_MSG_SIZE      512
#define IPC_MAX_MESSAGES      16

typedef enum {
    IPC_MSG,
    IPC_SYNC
} ipc_type_t;

typedef struct {
    u64 sender_pid;
    u64 size;
    u8 data[IPC_MAX_MSG_SIZE];
} ipc_message_t;

typedef struct {
    ipc_message_t messages[IPC_MAX_MESSAGES];
    u32 head;
    u32 tail;
    u32 count;
    bool in_use;
} ipc_channel_t;

// Initialize IPC subsystem
void ipc_init(void);

// Create/open a channel
int ipc_create(const char* name);

// Send message to channel
int ipc_send(int channel, void* data, u64 size);

// Receive message from channel
int ipc_recv(int channel, void* buf, u64 max_size);

// Close channel
void ipc_close(int channel);

// Shared memory
#define SHM_MAX_BLOCKS      16
#define SHM_BLOCK_SIZE      4096

int shm_alloc(void);
void* shm_get(int id);
void shm_free(int id);