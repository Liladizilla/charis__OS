/* partition.h - defensive read-only MBR and GPT metadata parsing. */
#ifndef CHARISOS_PARTITION_H
#define CHARISOS_PARTITION_H

#include <kernel/ata.h>

#define MBR_PARTITION_COUNT 4u
#define MBR_MAX_LOGICAL_PARTITIONS 32u
#define MBR_SECTOR_BYTES 512u
#define GPT_MAX_PARTITIONS 128u
#define GPT_MAX_ENTRY_SIZE 512u
#define GPT_MAX_ARRAY_BYTES 65536u
#define GPT_HEADER_MIN_SIZE 92u

typedef enum {
    PARTITION_SCHEME_NONE = 0,
    PARTITION_SCHEME_MBR,
    PARTITION_SCHEME_GPT_PROTECTIVE,
    PARTITION_SCHEME_INVALID
} partition_scheme_t;

typedef struct {
    bool present;
    bool bootable;
    bool accessible;
    bool extended;
    u8 type;
    u32 first_lba;
    u32 sector_count;
} mbr_partition_t;

typedef struct {
    partition_scheme_t scheme;
    bool valid;
    bool has_extended;
    bool fully_enumerated;
    u32 partition_count;
    mbr_partition_t partitions[MBR_PARTITION_COUNT];
    u32 extended_base_lba;
    u64 extended_end_lba;
    u32 logical_partition_count;
    mbr_partition_t logical_partitions[MBR_MAX_LOGICAL_PARTITIONS];
} partition_table_t;

typedef struct {
    bool has_next_ebr;
    u64 next_ebr_lba;
    mbr_partition_t logical_partition;
} ebr_sector_info_t;

typedef struct {
    u64 backup_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u64 entry_array_lba;
    u32 entry_count;
    u32 entry_size;
    u32 entry_array_bytes;
    u32 entry_array_sectors;
    u32 entry_array_crc32;
    u8 disk_guid[16];
} gpt_header_info_t;

typedef struct {
    bool present;
    bool accessible;
    u8 type_guid[16];
    u8 unique_guid[16];
    u64 first_lba;
    u64 last_lba;
    u64 attributes;
} gpt_partition_t;

typedef struct {
    bool valid;
    bool fully_enumerated;
    u32 partition_count;
    u32 entry_count;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    gpt_partition_t partitions[GPT_MAX_PARTITIONS];
} gpt_partition_table_t;

bool partition_parse_mbr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                partition_table_t* output);
bool partition_parse_ebr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                u64 ebr_lba,
                                u64 extended_base_lba,
                                u64 extended_end_lba,
                                const ata_device_info_t* device,
                                ebr_sector_info_t* output);
bool partition_parse_gpt_header(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                gpt_header_info_t* output);
bool partition_parse_gpt_entries(const gpt_header_info_t* header,
                                 const u8* entry_array,
                                 u32 entry_array_bytes,
                                 const ata_device_info_t* device,
                                 gpt_partition_table_t* output);

#endif
