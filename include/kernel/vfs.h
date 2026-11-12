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

/* Explicit per-open rights, intersected with the process capability set. */
#define FD_RIGHT_READ  (1U << 0)
#define FD_RIGHT_WRITE (1U << 1)
#define FD_RIGHT_MASK  (FD_RIGHT_READ | FD_RIGHT_WRITE)

/*
 * Non-standard descriptors are ABI-opaque, task-scoped identifiers:
 *   [ owner PID: 32 bits ][ generation: 26 bits ][ slot: 6 bits ]
 * The encoding is deterministic, not secret or unforgeable. It prevents stale
 * slot reuse and accidental cross-task resolution; it is not an authorization
 * token or an intra-task isolation boundary. Standard streams retain 0, 1, 2.
 */
typedef u64 fd_handle_t;
#define FD_HANDLE_SLOT_BITS       6U
#define FD_HANDLE_SLOT_MASK       ((1ULL << FD_HANDLE_SLOT_BITS) - 1ULL)
#define FD_HANDLE_GENERATION_MAX ((1U << 26) - 1U)
/* PID zero is never allocated; owner-PID zero reserves this invalid value. */
#define FD_HANDLE_INVALID         ((fd_handle_t)3ULL)
#define FD_HANDLE_SLOT(handle)    ((u32)((handle) & FD_HANDLE_SLOT_MASK))

/* VFS read/write callbacks must never access more than the requested
 * `size` bytes. They return a negative value on error or a byte count in
 * [0, size]; callers validate the reported count before using it. */
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
    u32 rights;
    /* Preserved across close and task-slot reuse; never silently wraps. */
    u32 generation;
} fd_entry_t;

void vfs_init(void);
vfs_node_t* vfs_resolve(const char* path);
int vfs_mount(const char* path, vfs_node_t* fs_root);

int fd_table_init(void);
typedef enum {
    FD_ALLOC_FAILED = 0,
    FD_ALLOC_OK = 1,
    FD_ALLOC_NODE_ALREADY_OWNED = 2
} fd_alloc_status_t;

/* The status output is required. Success transfers cleanup ownership to the
 * descriptor. NODE_ALREADY_OWNED means the caller must not invoke close;
 * other failures leave ownership with the caller. Ownership is enforced
 * globally, including concurrent allocations. Shared resources need a
 * reference-counted open-file object rather than shared raw node pointers. */
fd_handle_t fd_alloc(vfs_node_t* node, u32 flags, u32 rights,
                     fd_alloc_status_t* status);
bool fd_close(fd_handle_t handle);
/* Close non-standard descriptors owned by a task and restore standard streams. */
void fd_table_close_all(fd_entry_t* table);
fd_entry_t* fd_get(fd_handle_t handle); // Resolves only in the owning current task.
bool fd_has_rights(fd_handle_t handle, u32 rights);