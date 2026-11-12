#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel/fs.h>
#include <kernel/vfs.h>

/* Include the implementation to exercise the real static FAT32 adapters. */
#include "../kernel/vfs.c"

static u32 read_calls;
static u32 open_calls;
static u32 write_calls;
static file_t observed_file;

int fs_read(file_t* file, void* buffer, usize size) {
    read_calls++;
    observed_file = *file;
    memset(buffer, 0xA5, size);
    return (int)size;
}

int fs_open(const char* path, file_t* file) {
    open_calls++;
    assert(path != NULL);
    file->cluster = 7;
    file->size = 100;
    file->pos = 0;
    file->dir_cluster = 2;
    file->dir_sector = 0;
    file->dir_index = 0;
    return 0;
}

int fs_write(file_t* file, const void* buffer, usize size) {
    write_calls++;
    assert(buffer != NULL);
    observed_file = *file;
    file->pos += (u32)size;
    if (file->pos > file->size) file->size = file->pos;
    return (int)size;
}

static void reset_calls(void) {
    read_calls = 0;
    open_calls = 0;
    write_calls = 0;
    memset(&observed_file, 0, sizeof(observed_file));
}

int main(void) {
    vfs_node_t node = {0};
    u8 buffer[16] = {0};
    node.name[0] = 'T';
    node.name[1] = 0;
    node.size = 100;
    node.data = (void*)(uintptr_t)7;

    /* Oversized read offsets must fail before the FAT32 callback is entered. */
    reset_calls();
    assert(fat32_vfs_read(&node, 0x100000000ULL, sizeof(buffer), buffer) == -1);
    assert(fat32_vfs_read(&node, ~(u64)0, sizeof(buffer), buffer) == -1);
    assert(read_calls == 0);

    /* A valid 32-bit position is preserved exactly. */
    assert(fat32_vfs_read(&node, 37, sizeof(buffer), buffer) == (int)sizeof(buffer));
    assert(read_calls == 1);
    assert(observed_file.pos == 37);
    assert(observed_file.cluster == 7);
    assert(buffer[0] == 0xA5);

    /* Oversized writes must fail before opening or writing the file. */
    reset_calls();
    assert(fat32_vfs_write(&node, 0x100000000ULL, 4, buffer) == -1);
    assert(fat32_vfs_write(&node, ~(u64)0, 4, buffer) == -1);
    assert(open_calls == 0);
    assert(write_calls == 0);

    /* Valid writes preserve their 32-bit position and update node size. */
    assert(fat32_vfs_write(&node, 41, 4, buffer) == 4);
    assert(open_calls == 1);
    assert(write_calls == 1);
    assert(observed_file.pos == 41);
    assert(node.size == 45);

    puts("PASS: FAT32 VFS adapters reject 64-bit offsets before narrowing");
    return 0;
}
