/* cex.h - Charis Executable container, version 1.
 *
 * CEX wraps a native little-endian ELF64 executable with an architecture tag,
 * payload length and FNV-1a corruption checksum. The checksum is not a
 * cryptographic signature and must never be treated as one.
 */
#pragma once
#include <kernel/types.h>
#include <kernel/vfs.h>

#define CEX_VERSION_X86_64 1
#define CEX_MACHINE_X86_64 62
#define CEX_MAGIC_0 'C'
#define CEX_MAGIC_1 'E'
#define CEX_MAGIC_2 'X'
#define CEX_MAGIC_3 '1'

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u16 version;
    u16 machine;
    u32 header_size;
    u32 flags;
    u64 payload_size;
    u32 checksum;
    u32 reserved;
} cex_header_t;

/* Returns 1 for a verified CEX container, 0 for a raw ELF file, -1 if a
 * CEX signature is present but its header or payload is invalid. */
int cex_probe(vfs_node_t* node, u64* payload_offset, u64* payload_size);
