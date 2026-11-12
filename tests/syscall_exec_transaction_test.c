#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/security.h>
#include <kernel/vmm.h>
#include <kernel/elf.h>

/* Exercise the real syscall exec transaction with controlled VM/ELF services. */
#include "../kernel/syscall.c"

#define USER_PATH_BASE 0x4000ULL

static task_t active_task;
static pml4_t old_space __attribute__((aligned(4096)));
static pml4_t replacement_space __attribute__((aligned(4096)));
static u8 user_path[32];
static pml4_t* active_cr3;
static pml4_t* loader_space;
static pml4_t* destroyed_spaces[4];
static u32 destroyed_count;
static u32 create_calls;
static u32 copy_calls;
static u32 switch_calls;
static u32 loader_calls;
static u32 free_calls;
static bool create_succeeds;
static bool copy_succeeds;
static bool loader_succeeds;

task_t* scheduler_current(void) { return &active_task; }

bool security_check_capability(task_t* task, u32 capability) {
    return task == &active_task &&
        (capability == CAP_SPAWN || capability == CAP_ALLOW_EXEC);
}

bool security_verify_path(const char* path, task_t* task) {
    return task == &active_task && path && strcmp(path, "/APP.ELF") == 0;
}

void* kmalloc(usize size) { return malloc(size); }
void kfree(void* pointer) {
    if (pointer) free_calls++;
    free(pointer);
}
void serial_puts(const char* text) { (void)text; }
void security_audit(const char* event, task_t* task) {
    (void)event; (void)task;
}
void* kmemcpy(void* destination, const void* source, usize size) {
    return memcpy(destination, source, size);
}
usize kstrlen(const char* text) { return strlen(text); }

bool vmm_translate(pml4_t* pml4, u64 address, u64* physical, u64* flags) {
    if (pml4 != &old_space || !physical || !flags ||
        address < USER_PATH_BASE ||
        address - USER_PATH_BASE >= sizeof(user_path)) return false;
    *physical = (u64)(uintptr_t)&user_path[address - USER_PATH_BASE];
    *flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    return true;
}

void* vmm_phys_to_virt(u64 physical) {
    uintptr_t start = (uintptr_t)user_path;
    uintptr_t address = (uintptr_t)physical;
    if (address < start || address >= start + sizeof(user_path)) return NULL;
    return (void*)address;
}

pml4_t* vmm_create_address_space(void) {
    create_calls++;
    return create_succeeds ? &replacement_space : NULL;
}

bool vmm_copy_kernel_mappings(pml4_t* destination, pml4_t* source) {
    copy_calls++;
    assert(destination == &replacement_space);
    assert(source == NULL);
    return copy_succeeds;
}

void vmm_switch(pml4_t* next) {
    assert(next == &replacement_space);
    active_cr3 = next;
    switch_calls++;
}

void vmm_destroy_address_space(pml4_t* address_space) {
    assert(destroyed_count < 4);
    if (address_space == &old_space) {
        assert(active_cr3 == &replacement_space);
        assert(active_task.mm.pml4 == &replacement_space);
        assert(active_task.address_space == &replacement_space);
    }
    destroyed_spaces[destroyed_count++] = address_space;
}

int elf_load_exec(const char* path, process_mm_t* mm,
                  u64* entry_point, u64* user_rsp,
                  const char* const argv[], int argc,
                  const char* const envp[], int envc) {
    loader_calls++;
    assert(strcmp(path, "/APP.ELF") == 0);
    assert(mm != NULL && mm->pml4 == &replacement_space);
    assert(mm->heap_start == 0 && mm->heap_end == 0 && mm->vmas == NULL);
    assert(argv != NULL && envp != NULL);
    assert(argc == 0 && envc == 0);
    loader_space = mm->pml4;
    if (!loader_succeeds) return -1;
    mm->heap_start = 0x70000000ULL;
    mm->heap_end = 0x70000000ULL;
    *entry_point = 0x40000000ULL;
    *user_rsp = 0x00007FFFFFFFDF00ULL;
    mm->stack_top = *user_rsp;
    return 0;
}

static void reset_state(void) {
    memset(&active_task, 0, sizeof(active_task));
    memset(user_path, 0, sizeof(user_path));
    strcpy((char*)user_path, "/APP.ELF");
    memset(&old_space, 0, sizeof(old_space));
    memset(&replacement_space, 0, sizeof(replacement_space));
    active_task.pid = 73;
    active_task.is_user = true;
    active_task.capabilities = CAP_SPAWN | CAP_ALLOW_EXEC;
    active_task.mm.pml4 = &old_space;
    active_task.mm.heap_start = 0x10000000ULL;
    active_task.mm.heap_end = 0x10001000ULL;
    active_task.mm.stack_top = 0x7FFF0000ULL;
    active_task.address_space = &old_space;
    active_task.user_stack_base = 0x70000000ULL;
    active_task.user_rsp = 0x7FFF0000ULL;
    active_task.user_stack_pages[0] = 0xA000;
    active_task.user_stack_pages[1] = 0xB000;
    active_task.user_code_page_count = 1;
    active_task.user_code_pages[0] = 0xC000;
    active_task.fd_table[3].generation = 19;
    active_cr3 = &old_space;
    loader_space = NULL;
    memset(destroyed_spaces, 0, sizeof(destroyed_spaces));
    destroyed_count = 0;
    create_calls = 0;
    copy_calls = 0;
    switch_calls = 0;
    loader_calls = 0;
    free_calls = 0;
    create_succeeds = true;
    copy_succeeds = true;
    loader_succeeds = true;
}

static reg_frame_t make_exec_frame(void) {
    reg_frame_t frame = {0};
    frame.cs = 0x1B;
    frame.rdi = USER_PATH_BASE;
    frame.rsi = 0;
    frame.rdx = 0;
    frame.rip = 0x11112222ULL;
    frame.rsp = 0x33334444ULL;
    return frame;
}

static void test_success_commits_fresh_space_then_reclaims_old(void) {
    reset_state();
    reg_frame_t frame = make_exec_frame();
    u32 original_fd_generation = active_task.fd_table[3].generation;

    assert(syscall_exec_from_frame(&frame));
    assert(create_calls == 1 && copy_calls == 1 && loader_calls == 1);
    assert(loader_space == &replacement_space);
    assert(active_cr3 == &replacement_space && switch_calls == 1);
    assert(destroyed_count == 1 && destroyed_spaces[0] == &old_space);
    assert(active_task.mm.pml4 == &replacement_space);
    assert(active_task.address_space == &replacement_space);
    assert(active_task.mm.heap_start == 0x70000000ULL);
    assert(active_task.mm.heap_end == 0x70000000ULL);
    assert(active_task.mm.stack_top == 0x00007FFFFFFFDF00ULL);
    assert(active_task.user_rsp == 0x00007FFFFFFFDF00ULL);
    assert(active_task.user_stack_base == 0);
    assert(active_task.user_stack_pages[0] == 0);
    assert(active_task.user_stack_pages[1] == 0);
    assert(active_task.user_code_page_count == 0);
    assert(active_task.user_code_pages[0] == 0);
    assert(active_task.fd_table[3].generation == original_fd_generation);
    assert(frame.rip == 0x40000000ULL);
    assert(frame.rsp == 0x00007FFFFFFFDF00ULL);
    assert(frame.cs == 0x1B && frame.ss == 0x23);
    assert(frame.rflags == 0x202 && frame.rax == 0);
    assert(free_calls == 2);
}

static void test_clone_failure_preserves_old_process(void) {
    reset_state();
    copy_succeeds = false;
    reg_frame_t frame = make_exec_frame();
    u64 original_rip = frame.rip;
    assert(!syscall_exec_from_frame(&frame));
    assert(create_calls == 1 && copy_calls == 1 && loader_calls == 0);
    assert(active_cr3 == &old_space && switch_calls == 0);
    assert(destroyed_count == 1 && destroyed_spaces[0] == &replacement_space);
    assert(active_task.mm.pml4 == &old_space);
    assert(active_task.address_space == &old_space);
    assert(frame.rip == original_rip);
    assert(free_calls == 2);
}

static void test_loader_failure_preserves_old_process(void) {
    reset_state();
    loader_succeeds = false;
    reg_frame_t frame = make_exec_frame();
    u64 original_rip = frame.rip;
    assert(!syscall_exec_from_frame(&frame));
    assert(loader_calls == 1);
    assert(active_cr3 == &old_space && switch_calls == 0);
    assert(destroyed_count == 1 && destroyed_spaces[0] == &replacement_space);
    assert(active_task.mm.pml4 == &old_space);
    assert(active_task.address_space == &old_space);
    assert(frame.rip == original_rip);
    assert(free_calls == 2);
}

static void test_address_space_allocation_failure_preserves_old_process(void) {
    reset_state();
    create_succeeds = false;
    reg_frame_t frame = make_exec_frame();
    u64 original_rip = frame.rip;
    assert(!syscall_exec_from_frame(&frame));
    assert(create_calls == 1 && copy_calls == 0 && loader_calls == 0);
    assert(active_cr3 == &old_space && switch_calls == 0);
    assert(destroyed_count == 0);
    assert(active_task.mm.pml4 == &old_space);
    assert(frame.rip == original_rip);
    assert(free_calls == 2);
}

int main(void) {
    test_success_commits_fresh_space_then_reclaims_old();
    test_clone_failure_preserves_old_process();
    test_loader_failure_preserves_old_process();
    test_address_space_allocation_failure_preserves_old_process();
    puts("PASS: exec constructs a fresh process address space and commits only after ELF and stack validation");
    return 0;
}
