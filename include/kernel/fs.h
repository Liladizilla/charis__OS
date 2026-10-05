#ifndef FS_H
#define FS_H

#include <kernel/types.h>

typedef struct {
    u32 cluster;
    u32 size;
    u32 pos;
    /* Location of the directory entry so writes can update the size field. */
    u32 dir_cluster;
    u32 dir_sector;
    int  dir_index;
} file_t;

void fs_init(void);
int fs_open(const char* path, file_t* file);
int fs_read(file_t* file, void* buffer, usize size);
int fs_write(file_t* file, const void* buffer, usize size);
int fs_close(file_t* file);
int fs_create(const char* path);
void fs_self_test(void);

#endif