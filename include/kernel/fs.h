#ifndef FS_H
#define FS_H

#include <kernel/types.h>

typedef struct {
    u32 cluster;
    u32 size;
    u32 pos;
} file_t;

void fs_init(void);
int fs_open(const char* path, file_t* file);
int fs_read(file_t* file, void* buffer, usize size);
int fs_close(file_t* file);

#endif