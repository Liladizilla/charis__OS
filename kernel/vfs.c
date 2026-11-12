#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/task.h>
#include <kernel/string.h>

#define FD_NODE_OWNER_SLOTS 4093U
#define FD_NODE_OWNER_TOMBSTONE ((uintptr_t)1)
#define FD_NODE_OWNER_CLOSING   ((uintptr_t)1)

/* A closeable per-open node may belong to only one descriptor globally.
 * uintptr_t keys remain valid for registry comparisons after close callbacks
 * free their nodes. The low pointer bit marks a callback currently running. */
static uintptr_t fd_node_owners[FD_NODE_OWNER_SLOTS];

static u32 fd_node_owner_hash(uintptr_t key) {
    return (u32)((key >> 4) % FD_NODE_OWNER_SLOTS);
}

static bool fd_node_owner_claim(vfs_node_t* node) {
    if (!node || !node->close) return true;

    uintptr_t key = (uintptr_t)node;
    /* vfs_node_t alignment is at least 8 bytes; bit zero is reserved. */
    if (key & FD_NODE_OWNER_CLOSING) return false;
    uintptr_t closing_key = key | FD_NODE_OWNER_CLOSING;
    u32 start = fd_node_owner_hash(key);

retry:
    {
        u32 first_tombstone = FD_NODE_OWNER_SLOTS;
        for (u32 probe = 0; probe < FD_NODE_OWNER_SLOTS; probe++) {
            u32 slot = (start + probe) % FD_NODE_OWNER_SLOTS;
            uintptr_t current =
                __atomic_load_n(&fd_node_owners[slot], __ATOMIC_ACQUIRE);

            if (current == key) return false;
            if (current == closing_key) {
                /* The old node is being released. Its address may be reused
                 * only after the callback frees it and the owner is removed. */
                while (__atomic_load_n(&fd_node_owners[slot],
                                       __ATOMIC_ACQUIRE) == closing_key) {
                    asm volatile("pause" : : : "memory");
                }
                goto retry;
            }
            if (current == FD_NODE_OWNER_TOMBSTONE) {
                if (first_tombstone == FD_NODE_OWNER_SLOTS)
                    first_tombstone = slot;
                continue;
            }

            if (!current) {
                u32 target = first_tombstone == FD_NODE_OWNER_SLOTS
                           ? slot : first_tombstone;
                uintptr_t expected =
                    first_tombstone == FD_NODE_OWNER_SLOTS
                    ? 0 : FD_NODE_OWNER_TOMBSTONE;
                if (__atomic_compare_exchange_n(&fd_node_owners[target],
                        &expected, key, false, __ATOMIC_ACQ_REL,
                        __ATOMIC_ACQUIRE)) return true;
                if (expected == key) return false;
                goto retry;
            }
        }

        if (first_tombstone == FD_NODE_OWNER_SLOTS) return false;
        uintptr_t expected = FD_NODE_OWNER_TOMBSTONE;
        if (__atomic_compare_exchange_n(&fd_node_owners[first_tombstone],
                &expected, key, false, __ATOMIC_ACQ_REL,
                __ATOMIC_ACQUIRE)) return true;
        if (expected == key) return false;
        goto retry;
    }
}

/* Mark the owner as closing before calling its destructor. Only one caller
 * wins this transition, even if two close paths race on the same descriptor. */
static bool fd_node_owner_begin_release(uintptr_t key) {
    if (!key || (key & FD_NODE_OWNER_CLOSING)) return false;
    uintptr_t closing_key = key | FD_NODE_OWNER_CLOSING;
    u32 start = fd_node_owner_hash(key);

retry:
    for (u32 probe = 0; probe < FD_NODE_OWNER_SLOTS; probe++) {
        u32 slot = (start + probe) % FD_NODE_OWNER_SLOTS;
        uintptr_t current =
            __atomic_load_n(&fd_node_owners[slot], __ATOMIC_ACQUIRE);
        if (!current) return false;
        if (current == closing_key) return false;
        if (current == key) {
            uintptr_t expected = key;
            if (__atomic_compare_exchange_n(&fd_node_owners[slot], &expected,
                    closing_key, false, __ATOMIC_ACQ_REL,
                    __ATOMIC_ACQUIRE)) return true;
            if (expected == closing_key) return false;
            goto retry;
        }
    }
    return false;
}

/* Called after the close callback returns. This operates only on the saved
 * integer key and never dereferences a potentially freed vfs_node_t. */
static void fd_node_owner_finish_release(uintptr_t key) {
    if (!key || (key & FD_NODE_OWNER_CLOSING)) return;
    uintptr_t closing_key = key | FD_NODE_OWNER_CLOSING;
    u32 start = fd_node_owner_hash(key);

retry:
    for (u32 probe = 0; probe < FD_NODE_OWNER_SLOTS; probe++) {
        u32 slot = (start + probe) % FD_NODE_OWNER_SLOTS;
        uintptr_t current =
            __atomic_load_n(&fd_node_owners[slot], __ATOMIC_ACQUIRE);
        if (!current) return;
        if (current == closing_key) {
            uintptr_t expected = closing_key;
            if (__atomic_compare_exchange_n(&fd_node_owners[slot], &expected,
                    FD_NODE_OWNER_TOMBSTONE, false, __ATOMIC_RELEASE,
                    __ATOMIC_RELAXED)) return;
            goto retry;
        }
    }
}

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
    if (!node || !size) return -1;
    file_t file;
    if (fs_open(node->name, &file) != 0) return -1;
    file.pos = offset;
    int ret = fs_write(&file, buf, size);
    if (ret >= 0) {
        node->size = file.size;
    }
    return ret;
}

/* FAT lookup creates one heap-backed VFS node per successful open. The node
 * belongs to its descriptor and must be released on close/task exit. */
static void fat32_vfs_close(vfs_node_t* node) {
    if (node) kfree(node);
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
            /* kmalloc is not guaranteed to return zero-filled memory. */
            child->open = NULL;
            child->close = fat32_vfs_close;
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

/*
 * Resolve a descriptor handle in the current task. The owner PID prevents a
 * numeric handle copied from another task from resolving to an unrelated
 * object that happens to occupy the same table slot. The per-slot generation
 * prevents a closed handle from naming a later object after slot reuse.
 */
static bool fd_resolve(task_t* task, fd_handle_t handle, u32* slot_out) {
    if (!task || !slot_out) return false;

    if (handle <= FD_STDERR) {
        u32 slot = (u32)handle;
        if (!task->fd_table[slot].node) return false;
        *slot_out = slot;
        return true;
    }

    u32 owner_pid = (u32)(handle >> 32);
    u32 slot = FD_HANDLE_SLOT(handle);
    u32 generation = (u32)((handle >> FD_HANDLE_SLOT_BITS) &
                           FD_HANDLE_GENERATION_MAX);
    if (!task->pid || owner_pid != task->pid || slot < 3 ||
        slot >= MAX_FDS || !generation) return false;

    fd_entry_t* entry = &task->fd_table[slot];
    if (!entry->node || entry->generation != generation) return false;
    *slot_out = slot;
    return true;
}

fd_handle_t fd_alloc(vfs_node_t* node, u32 flags, u32 rights,
                       fd_alloc_status_t* status) {
    if (!status) return FD_HANDLE_INVALID;
    *status = FD_ALLOC_FAILED;

    task_t* task = scheduler_current();
    if (!task || !task->pid || !node || !rights ||
        (rights & ~FD_RIGHT_MASK)) return FD_HANDLE_INVALID;
    if ((rights & FD_RIGHT_READ) && !node->read) return FD_HANDLE_INVALID;
    if ((rights & FD_RIGHT_WRITE) && !node->write) return FD_HANDLE_INVALID;

    /* Find a usable slot before claiming the node. */
    u32 slot = MAX_FDS;
    for (u32 i = 3; i < MAX_FDS; i++) {
        fd_entry_t* entry = &task->fd_table[i];
        if (!entry->node && entry->generation < FD_HANDLE_GENERATION_MAX) {
            slot = i;
            break;
        }
    }
    if (slot == MAX_FDS) return FD_HANDLE_INVALID;

    bool closeable = node->close != NULL;
    if (closeable && !fd_node_owner_claim(node)) {
        *status = FD_ALLOC_NODE_ALREADY_OWNED;
        return FD_HANDLE_INVALID;
    }

    /* A task context is never scheduled simultaneously on two CPUs, so
     * this previously selected slot remains private until publication. */
    fd_entry_t* entry = &task->fd_table[slot];
    u32 generation = ++entry->generation;
    entry->node = node;
    entry->offset = 0;
    entry->flags = flags;
    entry->rights = rights;
    *status = FD_ALLOC_OK;
    return ((u64)task->pid << 32) |
           ((u64)generation << FD_HANDLE_SLOT_BITS) | (u64)slot;
}

bool fd_close(fd_handle_t handle) {
    task_t* task = scheduler_current();
    u32 slot = 0;
    if (!fd_resolve(task, handle, &slot) || slot < 3) return false;

    fd_entry_t* entry = &task->fd_table[slot];
    vfs_node_t* node = entry->node;
    /* Revoke the handle before running filesystem-owned close code. */
    entry->node = NULL;
    entry->offset = 0;
    entry->flags = 0;
    entry->rights = 0;
    if (node->close) {
        uintptr_t owner_key = (uintptr_t)node;
        if (fd_node_owner_begin_release(owner_key)) {
            node->close(node);
            fd_node_owner_finish_release(owner_key);
        }
    }
    return true;
}

void fd_table_close_all(fd_entry_t* table) {
    if (!table) return;
    /* Keep generation counters intact so reused task slots cannot revive tokens. */
    for (int i = 3; i < MAX_FDS; i++) {
        vfs_node_t* node = table[i].node;
        table[i].node = NULL;
        table[i].offset = 0;
        table[i].flags = 0;
        table[i].rights = 0;
        if (node && node->close) {
            uintptr_t owner_key = (uintptr_t)node;
            if (fd_node_owner_begin_release(owner_key)) {
                node->close(node);
                fd_node_owner_finish_release(owner_key);
            }
        }
    }

    extern vfs_node_t dev_kbd;
    extern vfs_node_t dev_vga;
    table[FD_STDIN].node = &dev_kbd;
    table[FD_STDIN].offset = 0;
    table[FD_STDIN].flags = 0;
    table[FD_STDIN].rights = FD_RIGHT_READ;
    table[FD_STDOUT].node = &dev_vga;
    table[FD_STDOUT].offset = 0;
    table[FD_STDOUT].flags = 0;
    table[FD_STDOUT].rights = FD_RIGHT_WRITE;
    table[FD_STDERR].node = &dev_vga;
    table[FD_STDERR].offset = 0;
    table[FD_STDERR].flags = 0;
    table[FD_STDERR].rights = FD_RIGHT_WRITE;
}

fd_entry_t* fd_get(fd_handle_t handle) {
    task_t* task = scheduler_current();
    u32 slot = 0;
    if (!fd_resolve(task, handle, &slot)) return NULL;
    return &task->fd_table[slot];
}

bool fd_has_rights(fd_handle_t handle, u32 rights) {
    if (!rights || (rights & ~FD_RIGHT_MASK)) return false;
    fd_entry_t* entry = fd_get(handle);
    return entry && (entry->rights & rights) == rights;
}
