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

/* Permanent reference keeps the standard stream descriptions alive. All
 * tasks bind their standard descriptor entries to these shared objects. */
static vfs_open_file_t std_input_file = {
    .node = &dev_kbd, .offset = 0, .open_flags = 0,
    .io_lock = 0, .lifecycle = 1
};
static vfs_open_file_t std_output_file = {
    .node = &dev_vga, .offset = 0, .open_flags = 0,
    .io_lock = 0, .lifecycle = 1
};

// FAT32 wrapper implementations
static int fat32_vfs_read(vfs_node_t* node, u64 offset, u64 size, u8* buf) {
    /* FAT32 file_t positions are 32-bit. Reject before narrowing so a caller
     * cannot wrap a large 64-bit VFS offset back to the start of a file. */
    if (!node || !buf || offset > 0xFFFFFFFFULL) return -1;
    file_t file;
    file.cluster = (u32)(uintptr_t)node->data;
    file.size = node->size;
    file.pos = offset;
    return fs_read(&file, buf, size);
}

static int fat32_vfs_write(vfs_node_t* node, u64 offset, u64 size, const u8* buf) {
    /* Validate the FAT32 position before fs_open() or the u64-to-u32 cast. */
    if (!node || !buf || !size || offset > 0xFFFFFFFFULL) return -1;
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
    
    /* Task descriptor tables are initialized by task_init() and bind to the
     * permanent standard-stream open-file descriptions above. */
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

/*
 * Descriptor-table mutation is protected independently from open-file
 * references. Interrupts are disabled while the lock is held so a timer
 * preemption cannot make the same CPU spin on a lock held by its interrupted
 * context. Hosted regression tests use the same atomic lock without privileged
 * CLI instructions.
 */
static volatile u32 fd_table_lock_word;

static u64 vfs_irq_save(void) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
    return 0;
#else
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void vfs_irq_restore(u64 flags) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
    (void)flags;
#else
    asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
#endif
}

static u64 fd_table_lock(void) {
    u64 flags = vfs_irq_save();
    for (;;) {
        u32 expected = 0;
        if (__atomic_compare_exchange_n(&fd_table_lock_word, &expected, 1,
                false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return flags;
        asm volatile("pause" : : : "memory");
    }
}

static void fd_table_unlock(u64 flags) {
    __atomic_store_n(&fd_table_lock_word, 0, __ATOMIC_RELEASE);
    vfs_irq_restore(flags);
}

/* Acquire only from a live reference. The packed closing bit makes the
 * final-release transition irreversible, even when references race on SMP. */
static bool fd_open_file_get(vfs_open_file_t* file) {
    if (!file) return false;
    u64 observed = __atomic_load_n(&file->lifecycle, __ATOMIC_ACQUIRE);
    for (;;) {
        u64 references = observed & VFS_OPEN_FILE_REF_MASK;
        if ((observed & VFS_OPEN_FILE_CLOSING) || !references ||
            references == VFS_OPEN_FILE_REF_MASK) return false;
        if (__atomic_compare_exchange_n(&file->lifecycle, &observed,
                observed + 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return true;
    }
}

void fd_release(vfs_open_file_t* file) {
    if (!file) return;

    u64 observed = __atomic_load_n(&file->lifecycle, __ATOMIC_ACQUIRE);
    for (;;) {
        u64 references = observed & VFS_OPEN_FILE_REF_MASK;
        if ((observed & VFS_OPEN_FILE_CLOSING) || !references) return;

        u64 desired = references == 1
                    ? VFS_OPEN_FILE_CLOSING
                    : observed - 1;
        if (!__atomic_compare_exchange_n(&file->lifecycle, &observed,
                desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            continue;

        if (references == 1) {
            /* No descriptor or in-flight operation can acquire this object
             * now. Filesystem callbacks and heap release run outside locks. */
            vfs_node_t* node = file->node;
            if (node && node->close) {
                uintptr_t owner_key = (uintptr_t)node;
                if (fd_node_owner_begin_release(owner_key)) {
                    node->close(node);
                    fd_node_owner_finish_release(owner_key);
                }
            }
            kfree(file);
        }
        return;
    }
}

u64 fd_file_io_lock(vfs_open_file_t* file) {
    if (!file) return 0;
    u64 flags = vfs_irq_save();
    while (__atomic_exchange_n(&file->io_lock, 1, __ATOMIC_ACQUIRE))
        asm volatile("pause" : : : "memory");
    return flags;
}

void fd_file_io_unlock(vfs_open_file_t* file, u64 irq_flags) {
    if (!file) return;
    __atomic_store_n(&file->io_lock, 0, __ATOMIC_RELEASE);
    vfs_irq_restore(irq_flags);
}

static void fd_table_set_standard_locked(fd_entry_t* table) {
    table[FD_STDIN].file = &std_input_file;
    table[FD_STDIN].rights = FD_RIGHT_READ;
    table[FD_STDOUT].file = &std_output_file;
    table[FD_STDOUT].rights = FD_RIGHT_WRITE;
    table[FD_STDERR].file = &std_output_file;
    table[FD_STDERR].rights = FD_RIGHT_WRITE;
}

void fd_table_init(fd_entry_t* table) {
    if (!table) return;
    u64 irq_flags = fd_table_lock();
    for (u32 i = 0; i < MAX_FDS; i++) {
        table[i].file = NULL;
        table[i].rights = 0;
        /* Generation is deliberately preserved for task-slot reuse. */
    }
    fd_table_set_standard_locked(table);
    fd_table_unlock(irq_flags);
}

/*
 * Resolve a descriptor while fd_table_lock is held. PID binding prevents a
 * token copied from another task from resolving an unrelated table entry;
 * generation prevents reuse from reviving a stale token.
 */
static bool fd_resolve_locked(task_t* task, fd_handle_t handle,
                              u32* slot_out) {
    if (!task || !slot_out || handle == FD_HANDLE_INVALID) return false;

    if (handle <= FD_STDERR) {
        u32 slot = (u32)handle;
        if (!task->fd_table[slot].file) return false;
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
    if (!entry->file || entry->generation != generation) return false;
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

    vfs_open_file_t* file = (vfs_open_file_t*)kmalloc(sizeof(*file));
    if (!file) return FD_HANDLE_INVALID;
    file->node = node;
    file->offset = 0;
    file->open_flags = flags;
    file->io_lock = 0;
    file->lifecycle = 1;

    /* One open description owns one closeable node. Duplication will share
     * this description rather than trying to claim the same raw node twice. */
    bool closeable = node->close != NULL;
    if (closeable && !fd_node_owner_claim(node)) {
        kfree(file);
        *status = FD_ALLOC_NODE_ALREADY_OWNED;
        return FD_HANDLE_INVALID;
    }

    u64 irq_flags = fd_table_lock();
    u32 slot = MAX_FDS;
    for (u32 i = 3; i < MAX_FDS; i++) {
        fd_entry_t* entry = &task->fd_table[i];
        if (!entry->file && entry->generation < FD_HANDLE_GENERATION_MAX) {
            slot = i;
            break;
        }
    }

    if (slot == MAX_FDS) {
        fd_table_unlock(irq_flags);
        if (closeable) {
            uintptr_t owner_key = (uintptr_t)node;
            if (fd_node_owner_begin_release(owner_key))
                fd_node_owner_finish_release(owner_key);
        }
        kfree(file);
        return FD_HANDLE_INVALID;
    }

    u32 generation = task->fd_table[slot].generation + 1;
    fd_handle_t handle = ((u64)task->pid << 32) |
                         ((u64)generation << FD_HANDLE_SLOT_BITS) |
                         (u64)slot;
    task->fd_table[slot].generation = generation;
    task->fd_table[slot].file = file;
    task->fd_table[slot].rights = rights;
    fd_table_unlock(irq_flags);

    *status = FD_ALLOC_OK;
    return handle;
}

vfs_open_file_t* fd_acquire(fd_handle_t handle, u32 required_rights) {
    if (!required_rights || (required_rights & ~FD_RIGHT_MASK)) return NULL;
    task_t* task = scheduler_current();
    if (!task) return NULL;

    u64 irq_flags = fd_table_lock();
    u32 slot = 0;
    if (!fd_resolve_locked(task, handle, &slot)) {
        fd_table_unlock(irq_flags);
        return NULL;
    }

    fd_entry_t* entry = &task->fd_table[slot];
    vfs_open_file_t* file = entry->file;
    if ((entry->rights & required_rights) != required_rights ||
        !fd_open_file_get(file)) {
        fd_table_unlock(irq_flags);
        return NULL;
    }

    fd_table_unlock(irq_flags);
    return file;
}

/*
 * Duplicate a live descriptor into another task without duplicating the raw
 * VFS node. Both entries reference the same open-file description, so they
 * intentionally share the file offset while retaining independent rights.
 *
 * The caller must be kernel code: no syscall exposes recipient selection.
 * A PID is resolved through the kernel task table, never through a userspace
 * pointer. The global descriptor lock prevents task-local close/exit cleanup
 * from racing publication on this uniprocessor kernel.
 */
fd_handle_t fd_duplicate_to(fd_handle_t source, u32 recipient_pid,
                            u32 requested_rights) {
    if (!requested_rights || (requested_rights & ~FD_RIGHT_MASK) ||
        !recipient_pid) return FD_HANDLE_INVALID;

    task_t* sender = scheduler_current();
    if (!sender) return FD_HANDLE_INVALID;

    u64 irq_flags = fd_table_lock();
    task_t* recipient = task_find_by_pid(recipient_pid);
    if (!recipient || recipient->state == TASK_STATE_UNUSED ||
        recipient->state == TASK_STATE_ALLOCATING ||
        recipient->state == TASK_STATE_EXITING ||
        recipient->state == TASK_STATE_ZOMBIE) {
        fd_table_unlock(irq_flags);
        return FD_HANDLE_INVALID;
    }

    u32 source_slot = 0;
    if (!fd_resolve_locked(sender, source, &source_slot)) {
        fd_table_unlock(irq_flags);
        return FD_HANDLE_INVALID;
    }

    fd_entry_t* source_entry = &sender->fd_table[source_slot];
    if ((source_entry->rights & requested_rights) != requested_rights) {
        fd_table_unlock(irq_flags);
        return FD_HANDLE_INVALID;
    }

    u32 destination_slot = MAX_FDS;
    for (u32 i = 3; i < MAX_FDS; i++) {
        fd_entry_t* entry = &recipient->fd_table[i];
        if (!entry->file && entry->generation < FD_HANDLE_GENERATION_MAX) {
            destination_slot = i;
            break;
        }
    }
    if (destination_slot == MAX_FDS) {
        fd_table_unlock(irq_flags);
        return FD_HANDLE_INVALID;
    }

    vfs_open_file_t* file = source_entry->file;
    /* Acquire before publishing. No operation below this point can fail. */
    if (!fd_open_file_get(file)) {
        fd_table_unlock(irq_flags);
        return FD_HANDLE_INVALID;
    }

    u32 generation = recipient->fd_table[destination_slot].generation + 1;
    fd_handle_t duplicate = ((u64)recipient->pid << 32) |
                            ((u64)generation << FD_HANDLE_SLOT_BITS) |
                            (u64)destination_slot;
    recipient->fd_table[destination_slot].generation = generation;
    recipient->fd_table[destination_slot].file = file;
    recipient->fd_table[destination_slot].rights = requested_rights;

    fd_table_unlock(irq_flags);
    return duplicate;
}

bool fd_close(fd_handle_t handle) {
    task_t* task = scheduler_current();
    if (!task) return false;

    u64 irq_flags = fd_table_lock();
    u32 slot = 0;
    if (!fd_resolve_locked(task, handle, &slot) || slot < 3) {
        fd_table_unlock(irq_flags);
        return false;
    }

    fd_entry_t* entry = &task->fd_table[slot];
    vfs_open_file_t* file = entry->file;
    /* Revoke before dropping the descriptor's reference or running callbacks. */
    entry->file = NULL;
    entry->rights = 0;
    fd_table_unlock(irq_flags);

    fd_release(file);
    return true;
}

void fd_table_close_all(fd_entry_t* table) {
    if (!table) return;
    vfs_open_file_t* release_list[MAX_FDS];
    u32 release_count = 0;

    u64 irq_flags = fd_table_lock();
    for (u32 i = 3; i < MAX_FDS; i++) {
        vfs_open_file_t* file = table[i].file;
        table[i].file = NULL;
        table[i].rights = 0;
        if (file) release_list[release_count++] = file;
    }
    fd_table_set_standard_locked(table);
    fd_table_unlock(irq_flags);

    /* Close callbacks may free filesystem nodes, so never run them under the
     * descriptor lock. Multiple entries may legitimately reference one file. */
    for (u32 i = 0; i < release_count; i++)
        fd_release(release_list[i]);
}

bool fd_has_rights(fd_handle_t handle, u32 rights) {
    vfs_open_file_t* file = fd_acquire(handle, rights);
    if (!file) return false;
    fd_release(file);
    return true;
}
