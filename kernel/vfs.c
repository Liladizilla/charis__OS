#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/task.h>

// VFS mount points
#define MAX_MOUNTS 16
typedef struct {
    char path[64];
    vfs_node_t* root;
} mount_t;
static mount_t mounts[MAX_MOUNTS];
static int mount_count = 0;

// Root filesystem (FAT32 on disk)
static vfs_node_t fat32_root = {
    .flags = VFS_DIR,
    .size = 0,
    .inode = 0,
    .data = NULL,
    .readdir = NULL,
    .finddir = NULL
};

// Device nodes - exported for use by task.c
vfs_node_t dev_null = {
    .flags = VFS_CHARDEV,
    .size = 0,
    .inode = 1
};

vfs_node_t dev_zero = {
    .flags = VFS_CHARDEV,
    .size = 0,
    .inode = 2
};

vfs_node_t dev_kbd = {
    .flags = VFS_CHARDEV,
    .size = 0,
    .inode = 3
};

vfs_node_t dev_vga = {
    .flags = VFS_CHARDEV,
    .size = 0,
    .inode = 4
};

// FAT32 wrapper implementations
static int fat32_vfs_read(vfs_node_t* node, u64 offset, u64 size, u8* buf) {
    file_t file;
    file.cluster = (u32)(uintptr_t)node->data;
    file.size = node->size;
    file.pos = offset;
    return fs_read(&file, buf, size);
}

static int fat32_vfs_write(vfs_node_t* node, u64 offset, u64 size, const u8* buf) {
    (void)node; (void)offset; (void)size; (void)buf;
    return -1; // Read-only for now
}

static vfs_node_t* fat32_vfs_finddir(vfs_node_t* node, const char* name) {
    (void)node;
    
    file_t file;
    if (fs_open(name, &file) == 0) {
        vfs_node_t* child = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
        if (child) {
            kstrncpy(child->name, name, 255);
            child->flags = VFS_FILE;
            child->size = file.size;
            child->inode = 0;
            child->data = (void*)(uintptr_t)file.cluster;
            child->read = fat32_vfs_read;
            child->write = fat32_vfs_write;
            child->readdir = NULL;
            child->finddir = NULL;
            return child;
        }
    }
    return NULL;
}

void vfs_init(void) {
    // Mount FAT32 as root
    fat32_root.data = NULL;
    kstrncpy(fat32_root.name, "fs", 255);
    vfs_mount("/", &fat32_root);
    
    // Mount devfs
    vfs_mount("/dev", &dev_null); // /dev points to dev_null for simplicity
    
    // Initialize file descriptor table
    fd_table_init();
    
    vga_puts("VFS initialized\n");
}

vfs_node_t* vfs_resolve(const char* path) {
    // Simplistic path resolution - only handles /dev/null and /dev/zero for now
    if (kstrcmp(path, "/dev/null") == 0) {
        return &dev_null;
    }
    if (kstrcmp(path, "/dev/zero") == 0) {
        return &dev_zero;
    }
    if (kstrcmp(path, "/dev/kbd") == 0) {
        return &dev_kbd;
    }
    if (kstrcmp(path, "/dev/vga") == 0) {
        return &dev_vga;
    }
    
    // For now, try FAT32 root
    if (path[0] == '/' && path[1] != '\0') {
        return fat32_vfs_finddir(&fat32_root, path + 1);
    }
    
    return &fat32_root;
}

int vfs_mount(const char* path, vfs_node_t* fs_root) {
    if (mount_count >= MAX_MOUNTS) return -1;
    
    kstrncpy(mounts[mount_count].path, path, 63);
    mounts[mount_count].root = fs_root;
    mount_count++;
    
    return 0;
}

int fd_table_init(void) {
    // Standard fds are pre-allocated in task_init
    return 0;
}

int fd_alloc(vfs_node_t* node, u32 flags) {
    task_t* task = scheduler_current();
    if (!task) return -1;
    
    for (int i = 3; i < MAX_FDS; i++) { // Start from 3, 0-2 are reserved
        if (task->fd_table[i].node == NULL) {
            task->fd_table[i].node = node;
            task->fd_table[i].offset = 0;
            task->fd_table[i].flags = flags;
            return i;
        }
    }
    return -1;
}

void fd_close(int fd) {
    task_t* task = scheduler_current();
    if (!task || fd < 0 || fd >= MAX_FDS) return;
    
    task->fd_table[fd].node = NULL;
    task->fd_table[fd].offset = 0;
    task->fd_table[fd].flags = 0;
}

fd_entry_t* fd_get(int fd) {
    task_t* task = scheduler_current();
    if (!task || fd < 0 || fd >= MAX_FDS) return NULL;
    return &task->fd_table[fd];
}