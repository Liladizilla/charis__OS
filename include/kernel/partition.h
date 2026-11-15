/* partition.h - read-only legacy MBR partition metadata. */
#ifndef CHARISOS_PARTITION_H
#define CHARISOS_PARTITION_H

#include <kernel/ata.h>

#define MBR_PARTITION_COUNT 4u
#define MBR_SECTOR_BYTES 512u

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
} partition_table_t;

/* Parses exactly one 512-byte MBR sector. It does not follow EBR chains or
 * read GPT headers/arrays; those schemes remain explicitly incomplete. */
bool partition_parse_mbr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                partition_table_t* output);

#endif
