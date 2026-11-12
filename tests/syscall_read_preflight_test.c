#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>
#include <kernel/security.h>
#include <kernel/keyboard.h>

/* Include the implementation so the host test can exercise the real static
 * syscall handler and its real user-range/copy helpers. */
#include "../kernel/syscall.c"

static task_t test_task;
static pml4_t test_pml4;
static vfs_node_t test_node;
static fd_entry_t test_entry;
static u8 user_memory[PAGE_SIZE * 2];
static bool map_second_page;
static u64 user_page_flags = VMM_FLAG_USER | VMM_FLAG_WRITABLE;
static u32 keyboard_reads;
static u32 file_reads;

task_t* scheduler_current(void) {
    return &test_task;
}

fd_entry_t* fd_get(fd_handle_t handle) {
    return (handle == FD_STDIN || handle == 3) ? &test_entry : NULL;
}

bool security_check_capability(task_t* task, u32 capability) {
    return task == &test_task && capability == CAP_FS_READ;
}

void* kmalloc(usize size) {
    return malloc(size);
}

void kfree(void* pointer) {
    free(pointer);
}

bool keyboard_get_key(char* out) {
    keyboard_reads++;
    if (!out) return false;
    *out = 'K';
    return true;
}

bool vmm_translate(pml4_t* pml4, u64 address, u64* physical, u64* flags) {
    if (pml4 != &test_pml4 || !physical || !flags || address < 0x1000)
        return false;
    u64 offset = address - 0x1000;
    if (offset >= PAGE_SIZE && !map_second_page) return false;
    if (offset >= sizeof(user_memory)) return false;
    *physical = (u64)(uintptr_t)&user_memory[offset];
    *flags = VMM_FLAG_PRESENT | user_page_flags;
    return true;
}

void* vmm_phys_to_virt(u64 physical) {
    uintptr_t start = (uintptr_t)user_memory;
    uintptr_t address = (uintptr_t)physical;
    if (address < start || address >= start + sizeof(user_memory)) return NULL;
    return (void*)address;
}

void* kmemcpy(void* destination, const void* source, usize size) {
    return memcpy(destination, source, size);
}

static int test_file_read(vfs_node_t* node, u64 offset, u64 size, u8* buffer) {
    (void)node;
    (void)offset;
    file_reads++;
    for (u64 i = 0; i < size; i++) buffer[i] = (u8)('A' + (i % 26));
    return (int)size;
}

static void reset_test_state(void) {
    memset(&test_task, 0, sizeof(test_task));
    memset(&test_entry, 0, sizeof(test_entry));
    memset(&test_node, 0, sizeof(test_node));
    memset(user_memory, 0, sizeof(user_memory));
    test_task.is_user = true;
    test_task.mm.pml4 = &test_pml4;
    test_node.read = test_file_read;
    test_entry.node = &test_node;
    test_entry.rights = FD_RIGHT_READ;
    test_entry.offset = 13;
    map_second_page = false;
    user_page_flags = VMM_FLAG_USER | VMM_FLAG_WRITABLE;
    keyboard_reads = 0;
    file_reads = 0;
}

int main(void) {
    reset_test_state();

    /* An unmapped stdin destination must not consume a keyboard character. */
    assert(syscall_read_handler(FD_STDIN, 0x2000, 1, 0, 0, 0) == (u64)-1);
    assert(keyboard_reads == 0);

    /* A user mapping without effective write permission is also rejected. */
    user_page_flags = VMM_FLAG_USER;
    assert(syscall_read_handler(FD_STDIN, 0x1000, 1, 0, 0, 0) == (u64)-1);
    assert(keyboard_reads == 0);

    user_page_flags = VMM_FLAG_USER | VMM_FLAG_WRITABLE;
    assert(syscall_read_handler(FD_STDIN, 0x1000, 1, 0, 0, 0) == 1);
    assert(keyboard_reads == 1);
    assert(user_memory[0] == 'K');

    /* A buffer that crosses into an unmapped page must not advance the VFS. */
    assert(syscall_read_handler(3, 0x1FF0, 32, 0, 0, 0) == (u64)-1);
    assert(file_reads == 0);
    assert(test_entry.offset == 13);

    /* Cross-page reads work once both pages are mapped writable for userspace. */
    map_second_page = true;
    assert(syscall_read_handler(3, 0x1FF0, 32, 0, 0, 0) == 32);
    assert(file_reads == 1);
    assert(test_entry.offset == 45);
    assert(user_memory[PAGE_SIZE - 16] == 'A');
    assert(user_memory[PAGE_SIZE + 15] == 'F');

    /* Zero-length reads do not require or inspect a user pointer. */
    assert(syscall_read_handler(3, 0, 0, 0, 0, 0) == 0);
    assert(file_reads == 1);

    /* Reject a range that would cross the lower canonical user boundary. */
    assert(syscall_read_handler(3, 0x00007FFFFFFFFFFFULL, 2, 0, 0, 0) == (u64)-1);
    assert(file_reads == 1);
    assert(test_entry.offset == 45);

    puts("PASS: SYS_READ validates the full writable destination before input side effects");
    return 0;
}
