#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/vfs.h>

#include "../kernel/elf.c"

static fd_entry_t test_entry;
static vfs_node_t test_node;
static u32 read_calls;
static u32 read_mode;

enum {
    READ_FULL = 0,
    READ_PARTIAL = 1,
    READ_OVERREPORT = 2,
    READ_EMPTY = 3
};

fd_entry_t* fd_get(fd_handle_t handle) {
    return handle == 17 ? &test_entry : NULL;
}

static int test_read(vfs_node_t* node, u64 offset, u64 size, u8* buffer) {
    (void)node;
    assert(offset == 0x100);
    read_calls++;

    if (read_mode == READ_OVERREPORT) return (int)size + 1;
    if (read_mode == READ_EMPTY) return 0;

    usize amount = (usize)size;
    if (read_mode == READ_PARTIAL && read_calls == 1 && amount > 1)
        amount /= 2;
    memset(buffer, (int)(0x40 + read_calls), amount);
    return (int)amount;
}

static void reset_state(void) {
    memset(&test_entry, 0, sizeof(test_entry));
    memset(&test_node, 0, sizeof(test_node));
    test_node.read = test_read;
    test_entry.node = &test_node;
    test_entry.rights = FD_RIGHT_READ;
    read_calls = 0;
    read_mode = READ_FULL;
}

int main(void) {
    u8 buffer[8];

    reset_state();
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == 8);
    assert(read_calls == 1);
    for (u32 i = 0; i < sizeof(buffer); i++) assert(buffer[i] == 0x41);

    reset_state();
    read_mode = READ_PARTIAL;
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == 8);
    assert(read_calls == 2);
    for (u32 i = 0; i < 4; i++) assert(buffer[i] == 0x41);
    for (u32 i = 4; i < 8; i++) assert(buffer[i] == 0x42);

    reset_state();
    read_mode = READ_OVERREPORT;
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == -1);
    assert(read_calls == 1);

    reset_state();
    read_mode = READ_EMPTY;
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == -1);
    assert(read_calls == 1);

    reset_state();
    assert(fd_read_all(17, ~(u64)0 - 3, buffer, sizeof(buffer)) == -1);
    assert(read_calls == 0);

    reset_state();
    assert(fd_read_all(17, ~(u64)0, buffer, 0) == 0);
    assert(read_calls == 0);

    reset_state();
    test_entry.rights = 0;
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == -1);
    assert(read_calls == 0);

    reset_state();
    test_entry.node = NULL;
    assert(fd_read_all(17, 0x100, buffer, sizeof(buffer)) == -1);
    assert(read_calls == 0);

    puts("PASS: ELF exact reads reject over-reported lengths and offset overflow");
    return 0;
}
