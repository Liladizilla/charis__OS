/* vfs.h - Virtual Filesystem Switch */
#pragma once
#include <kernel/types.h>

#define VFS_FILE      1
#define VFS_DIR       2
#define VFS_CHARDEV 3
#define VFS_BLOCKDEV 4

#define MAX_FDS 64
#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

typedef struct vfs_node {
    char name[256];
    u32 flags;
    u64 size;
    u64 inode;
    void* data; // Private filesystem data
    
    int (*read)(struct vfs_node*, u64 offset, u64 size, u8* buf);
    int (*write)(struct vfs_node*, u64 offset, u64 size, const u8* buf);
    int (*open)(struct vfs_node*, u32 flags);
    void (*close)(struct vfs_node*);
    struct vfs_node* (*readdir)(struct vfs_node*, u32 index);
    struct vfs_node* (*finddir)(struct vfs_node*, const char* name);
} vfs_node_t;

typedef struct fd_entry {
    vfs_node_t* node;
    u64 offset;
    u32 flags;
} fd_entry_t;

void vfs_init(void);
vfs_node_t* vfs_resolve(const char* path);
int vfs_mount(const char* path, vfs_node_t* fs_root);

int fd_table_init(void);
int fd_alloc(vfs_node_t* node, u32 flags);
void fd_close(int fd);
fd_entry_t* fd_get(int fd); // Gets from current task's fd table