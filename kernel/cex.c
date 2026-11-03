#include <kernel/cex.h>
#include <kernel/string.h>

#define CEX_FNV_OFFSET 2166136261U
#define CEX_FNV_PRIME  16777619U
#define CEX_READ_CHUNK 512U

static u32 cex_fnv_update(u32 hash, const u8* data, usize size) {
    for (usize i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= CEX_FNV_PRIME;
    }
    return hash;
}

int cex_probe(vfs_node_t* node, u64* payload_offset, u64* payload_size) {
    if (!node || !(node->flags & VFS_FILE) || !node->read ||
        !payload_offset || !payload_size) return -1;

    *payload_offset = 0;
    *payload_size = node->size;

    u8 magic[4];
    if (node->size < sizeof(magic) ||
        node->read(node, 0, sizeof(magic), magic) != (int)sizeof(magic)) return 0;

    if (magic[0] != CEX_MAGIC_0 || magic[1] != CEX_MAGIC_1 ||
        magic[2] != CEX_MAGIC_2 || magic[3] != CEX_MAGIC_3) return 0;

    cex_header_t header;
    if (node->size < sizeof(header) ||
        node->read(node, 0, sizeof(header), (u8*)&header) != (int)sizeof(header))
        return -1;

    if (header.version != CEX_VERSION_X86_64 ||
        header.machine != CEX_MACHINE_X86_64 ||
        header.header_size < sizeof(header) ||
        header.header_size > node->size ||
        header.flags != 0 || header.reserved != 0 ||
        header.payload_size < 64 ||
        header.payload_size != node->size - header.header_size) return -1;

    u8 buffer[CEX_READ_CHUNK];
    u64 offset = 0;
    u32 checksum = CEX_FNV_OFFSET;
    while (offset < header.payload_size) {
        usize chunk = (usize)(header.payload_size - offset);
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        int count = node->read(node, header.header_size + offset, chunk, buffer);
        if (count <= 0 || (usize)count != chunk) return -1;
        checksum = cex_fnv_update(checksum, buffer, chunk);
        offset += chunk;
    }

    if (checksum != header.checksum) return -1;
    *payload_offset = header.header_size;
    *payload_size = header.payload_size;
    return 1;
}
