#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/il_runtime.h>
#include <kernel/task.h>
#include <kernel/vfs.h>
#include <kernel/security.h>

#include "../kernel/il_runtime.c"

static task_t active_task;
static vfs_node_t test_node;
static u32 resolve_calls;
static u32 read_calls;
static u32 close_calls;
static bool allow_path;
static int read_result;
static bool resolve_node;

task_t* scheduler_current(void) { return &active_task; }

bool security_verify_path(const char* path, task_t* task) {
    return allow_path && path && path[0] == '/' && task == &active_task;
}

vfs_node_t* vfs_resolve(const char* path) {
    (void)path;
    resolve_calls++;
    return resolve_node ? &test_node : NULL;
}

static int test_read(vfs_node_t* node, u64 offset, u64 size, u8* buffer) {
    (void)node;
    assert(offset == 0);
    read_calls++;
    if (read_result > 0 && (u64)read_result <= size)
        memset(buffer, 0x5A, (size_t)read_result);
    return read_result;
}

static void test_close(vfs_node_t* node) {
    assert(node == &test_node);
    close_calls++;
}

static void reset_state(void) {
    memset(&active_task, 0, sizeof(active_task));
    memset(&test_node, 0, sizeof(test_node));
    test_node.flags = VFS_FILE;
    test_node.read = test_read;
    test_node.close = test_close;
    test_node.size = sizeof(il_instruction_t) * 2;
    resolve_calls = 0;
    read_calls = 0;
    close_calls = 0;
    allow_path = true;
    read_result = (int)test_node.size;
    resolve_node = true;
}

int main(void) {
    il_instruction_t program[IL_MAX_INSTRUCTIONS];
    reset_state();

    assert(il_load_bytecode("/PROGRAM.IL", program) == (int)test_node.size);
    assert(resolve_calls == 1 && read_calls == 1 && close_calls == 1);
    assert(((unsigned char*)program)[0] == 0x5A);

    reset_state();
    test_node.flags = VFS_DIR;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(read_calls == 0 && close_calls == 1);

    reset_state();
    read_result = -1;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(read_calls == 1 && close_calls == 1);

    reset_state();
    read_result = (int)test_node.size - 1;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(read_calls == 1 && close_calls == 1);

    reset_state();
    test_node.size = sizeof(program) + sizeof(il_instruction_t);
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(read_calls == 0 && close_calls == 1);

    reset_state();
    test_node.size = sizeof(il_instruction_t) + 1;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(read_calls == 0 && close_calls == 1);

    reset_state();
    allow_path = false;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(resolve_calls == 0 && close_calls == 0);

    reset_state();
    active_task.is_user = true;
    assert(il_load_bytecode("/PROGRAM.IL", program) == -1);
    assert(resolve_calls == 0 && close_calls == 0);

    reset_state();
    assert(il_load_bytecode("/PROGRAM.IL", NULL) == -1);
    assert(resolve_calls == 0 && close_calls == 0);

    puts("PASS: IL loader validates bytecode bounds and releases VFS nodes on every path");
    return 0;
}
