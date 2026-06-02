/* pipe.h - Anonymous pipes for IPC */
#pragma once
#include <kernel/types.h>

#define PIPE_MAX_PIPES    32
#define PIPE_BUFFER_SIZE  4096

typedef struct {
    uint8_t buffer[PIPE_BUFFER_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    task_t* reader;
    task_t* writer;
    int read_fd;
    int write_fd;
} pipe_t;

void pipe_init(void);

int pipe_create(void);
int pipe_read(int fd, void* buf, usize count);
int pipe_write(int fd, const void* buf, usize count);
void pipe_close(int fd);
bool pipe_can_read(int fd);
bool pipe_can_write(int fd);