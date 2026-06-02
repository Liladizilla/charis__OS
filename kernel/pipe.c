#include <kernel/pipe.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>

static pipe_t pipes[PIPE_MAX_PIPES];

void pipe_init(void) {
    for (int i = 0; i < PIPE_MAX_PIPES; i++) {
        pipes[i].read_pos = 0;
        pipes[i].write_pos = 0;
        pipes[i].count = 0;
        pipes[i].reader = NULL;
        pipes[i].writer = NULL;
        pipes[i].read_fd = -1;
        pipes[i].write_fd = -1;
    }
    vga_puts("Pipe subsystem initialized\n");
}

int pipe_create(void) {
    for (int i = 0; i < PIPE_MAX_PIPES; i++) {
        if (pipes[i].read_fd == -1) {
            pipes[i].count = 0;
            pipes[i].read_pos = 0;
            pipes[i].write_pos = 0;
            pipes[i].read_fd = i * 2;
            pipes[i].write_fd = i * 2 + 1;
            pipes[i].reader = NULL;
            pipes[i].writer = NULL;
            return i; /* Return pipe index */
        }
    }
    return -1;
}

int pipe_read(int fd, void* buf, usize count) {
    int pipe_idx = fd / 2;
    if (pipe_idx < 0 || pipe_idx >= PIPE_MAX_PIPES) return -1;
    
    pipe_t* pipe = &pipes[pipe_idx];
    if (buf == NULL) return -1;
    
    /* If pipe is empty and writer closed, return EOF */
    if (pipe->count == 0 && pipe->write_fd == -1) return 0;
    
    /* If pipe is empty, block the task (Phase 7 requirement) */
    if (pipe->count == 0) {
        task_t* task = scheduler_current();
        if (task) {
            pipe->reader = task;
            scheduler_block_task(task);
            return -2; /* Would block - caller should yield */
        }
    }
    
    usize to_read = count;
    if (to_read > pipe->count) to_read = pipe->count;
    
    uint8_t* b = (uint8_t*)buf;
    for (usize i = 0; i < to_read; i++) {
        b[i] = pipe->buffer[(pipe->read_pos + i) % PIPE_BUFFER_SIZE];
    }
    
    pipe->read_pos = (pipe->read_pos + to_read) % PIPE_BUFFER_SIZE;
    pipe->count -= to_read;
    
    /* Wake up writer if it was blocked */
    if (pipe->writer) {
        scheduler_unblock_task(pipe->writer);
        pipe->writer = NULL;
    }
    
    return (int)to_read;
}

int pipe_write(int fd, const void* buf, usize count) {
    int pipe_idx = (fd - 1) / 2;
    if (pipe_idx < 0 || pipe_idx >= PIPE_MAX_PIPES) return -1;
    
    pipe_t* pipe = &pipes[pipe_idx];
    if (buf == NULL) return -1;
    
    /* If pipe is full, block the task (Phase 7 requirement) */
    if (pipe->count >= PIPE_BUFFER_SIZE) {
        task_t* task = scheduler_current();
        if (task) {
            pipe->writer = task;
            scheduler_block_task(task);
            return -2; /* Would block - caller should yield */
        }
    }
    
    usize to_write = count;
    usize space = PIPE_BUFFER_SIZE - pipe->count;
    if (to_write > space) to_write = space;
    
    const uint8_t* b = (const uint8_t*)buf;
    for (usize i = 0; i < to_write; i++) {
        pipe->buffer[(pipe->write_pos + i) % PIPE_BUFFER_SIZE] = b[i];
    }
    
    pipe->write_pos = (pipe->write_pos + to_write) % PIPE_BUFFER_SIZE;
    pipe->count += to_write;
    
    /* Wake up reader if it was blocked */
    if (pipe->reader) {
        scheduler_unblock_task(pipe->reader);
        pipe->reader = NULL;
    }
    
    return (int)to_write;
}

void pipe_close(int fd) {
    int pipe_idx;
    bool is_read = false;
    
    if (fd % 2 == 0) {
        pipe_idx = fd / 2;
        is_read = true;
    } else {
        pipe_idx = (fd - 1) / 2;
    }
    
    if (pipe_idx < 0 || pipe_idx >= PIPE_MAX_PIPES) return;
    
    pipe_t* pipe = &pipes[pipe_idx];
    if (is_read) {
        pipe->read_fd = -1;
        /* Wake any blocked writer with error */
        if (pipe->writer) {
            scheduler_unblock_task(pipe->writer);
            pipe->writer = NULL;
        }
    } else {
        pipe->write_fd = -1;
        /* Wake any blocked reader - they get EOF */
        if (pipe->reader) {
            scheduler_unblock_task(pipe->reader);
            pipe->reader = NULL;
        }
    }
}

bool pipe_can_read(int fd) {
    int pipe_idx = fd / 2;
    if (pipe_idx < 0 || pipe_idx >= PIPE_MAX_PIPES) return false;
    return pipes[pipe_idx].count > 0;
}

bool pipe_can_write(int fd) {
    int pipe_idx = (fd - 1) / 2;
    if (pipe_idx < 0 || pipe_idx >= PIPE_MAX_PIPES) return false;
    return pipes[pipe_idx].count < (PIPE_BUFFER_SIZE - 1);
}