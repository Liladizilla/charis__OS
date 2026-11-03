#include <kernel/cex.h>
#include <assert.h>
#include <stdio.h>

static u8 image[sizeof(cex_header_t) + 64];

static int memory_read(vfs_node_t* node, u64 offset, u64 size, u8* output) {
    if (!node || !output || offset > node->size || size > node->size - offset)
        return -1;
    for (u64 i = 0; i < size; i++) output[i] = image[offset + i];
    return (int)size;
}

static u32 checksum(const u8* data, u32 size) {
    u32 value = 2166136261U;
    for (u32 i = 0; i < size; i++) {
        value ^= data[i];
        value *= 16777619U;
    }
    return value;
}

static void prepare(vfs_node_t* node) {
    for (u32 i = 0; i < 64; i++) image[sizeof(cex_header_t) + i] = (u8)(i * 3U + 1U);
    cex_header_t header = {
        .magic = { 'C', 'E', 'X', '1' },
        .version = CEX_VERSION_X86_64,
        .machine = CEX_MACHINE_X86_64,
        .header_size = sizeof(cex_header_t),
        .flags = 0,
        .payload_size = 64,
        .checksum = checksum(image + sizeof(cex_header_t), 64),
        .reserved = 0
    };
    const u8* source = (const u8*)&header;
    for (u32 i = 0; i < sizeof(header); i++) image[i] = source[i];

    *node = (vfs_node_t){0};
    node->flags = VFS_FILE;
    node->size = sizeof(image);
    node->read = memory_read;
}

int main(void) {
    vfs_node_t node;
    u64 offset = 0, size = 0;
    prepare(&node);

    assert(cex_probe(&node, &offset, &size) == 1);
    assert(offset == sizeof(cex_header_t));
    assert(size == 64);

    /* A damaged payload must fail checksum validation. */
    image[sizeof(cex_header_t) + 7] ^= 0x80;
    assert(cex_probe(&node, &offset, &size) == -1);
    prepare(&node);

    /* A CEX signature with an unexpected header size is malformed, not raw ELF. */
    cex_header_t* header = (cex_header_t*)image;
    header->header_size++;
    assert(cex_probe(&node, &offset, &size) == -1);
    prepare(&node);

    header = (cex_header_t*)image;
    header->flags = 1;
    assert(cex_probe(&node, &offset, &size) == -1);
    prepare(&node);

    header = (cex_header_t*)image;
    header->payload_size--;
    assert(cex_probe(&node, &offset, &size) == -1);

    /* Non-CEX files remain eligible for the raw ELF compatibility path. */
    prepare(&node);
    image[0] = 0x7F;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    assert(cex_probe(&node, &offset, &size) == 0);
    assert(offset == 0 && size == node.size);

    puts("CEX kernel parser tests passed");
    return 0;
}
