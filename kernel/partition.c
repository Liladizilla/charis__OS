/* partition.c - defensive, read-only MBR partition-table parser. */
#include <kernel/partition.h>

static void clear_output(partition_table_t* output) {
    u8* bytes = (u8*)output;
    for (u32 i = 0; i < sizeof(*output); i++) bytes[i] = 0;
}

static u32 read_le32(const u8* bytes) {
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static bool is_extended_type(u8 type) {
    return type == 0x05u || type == 0x0Fu || type == 0x85u;
}

static bool ranges_overlap(u64 first_a, u64 end_a,
                           u64 first_b, u64 end_b) {
    return first_a < end_b && first_b < end_a;
}

static bool reject_table(partition_table_t* output) {
    output->scheme = PARTITION_SCHEME_INVALID;
    output->valid = false;
    output->fully_enumerated = false;
    return false;
}

bool partition_parse_mbr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                partition_table_t* output) {
    if (!sector || !device || !output) return false;
    clear_output(output);

    output->scheme = PARTITION_SCHEME_NONE;
    output->fully_enumerated = false;

    if (!device->detected || !device->capacity_sectors ||
        device->logical_sector_bytes != MBR_SECTOR_BYTES) {
        output->scheme = PARTITION_SCHEME_INVALID;
        return false;
    }

    /* The MBR trailer is little-endian 0xAA55. A missing signature means no
     * MBR was recognized; this is not silently treated as an empty table. */
    if (sector[510] != 0x55u || sector[511] != 0xAAu)
        return true;

    output->scheme = PARTITION_SCHEME_MBR;
    output->valid = true;
    output->fully_enumerated = true;

    u32 extended_count = 0;
    u32 protective_count = 0;

    for (u32 index = 0; index < MBR_PARTITION_COUNT; index++) {
        const u8* raw = &sector[446u + index * 16u];
        mbr_partition_t* entry = &output->partitions[index];

        u8 boot_flag = raw[0];
        u8 type = raw[4];
        u32 first_lba = read_le32(&raw[8]);
        u32 sector_count = read_le32(&raw[12]);

        if (type == 0u) {
            /* An unused entry must be wholly empty. Stale LBA values or a boot
             * marker in an empty slot are rejected rather than guessed away. */
            if (boot_flag || first_lba || sector_count)
                return reject_table(output);
            continue;
        }

        if ((boot_flag != 0u && boot_flag != 0x80u) ||
            first_lba == 0u || sector_count == 0u)
            return reject_table(output);

        u64 end_lba = (u64)first_lba + (u64)sector_count;
        if (end_lba <= first_lba || end_lba > device->capacity_sectors)
            return reject_table(output);

        entry->present = true;
        entry->bootable = boot_flag == 0x80u;
        entry->type = type;
        entry->first_lba = first_lba;
        entry->sector_count = sector_count;
        entry->accessible = end_lba <= device->addressable_sectors;
        entry->extended = is_extended_type(type);
        output->partition_count++;

        if (entry->extended) {
            output->has_extended = true;
            output->fully_enumerated = false;
            extended_count++;
        }
        if (type == 0xEEu) protective_count++;
    }

    if (extended_count > 1u)
        return reject_table(output);

    /* Extended partitions require following EBR links. Do not claim a
     * complete inventory when the parser has only seen primary entries. */

    if (protective_count) {
        /* Hybrid MBR layouts need separate policy. Fail closed until GPT array
         * parsing exists instead of treating hybrid entries as ordinary MBR. */
        if (protective_count != 1u || output->partition_count != 1u)
            return reject_table(output);

        const mbr_partition_t* protective = NULL;
        for (u32 i = 0; i < MBR_PARTITION_COUNT; i++) {
            if (output->partitions[i].present) {
                protective = &output->partitions[i];
                break;
            }
        }
        if (!protective || protective->first_lba != 1u)
            return reject_table(output);

        output->scheme = PARTITION_SCHEME_GPT_PROTECTIVE;
        output->fully_enumerated = false;
    }

    /* Reject overlapping primary regions. Extended partitions are compared
     * as their own container entries; logical children are not read here. */
    for (u32 i = 0; i < MBR_PARTITION_COUNT; i++) {
        const mbr_partition_t* a = &output->partitions[i];
        if (!a->present) continue;
        u64 a_start = a->first_lba;
        u64 a_end = a_start + a->sector_count;

        for (u32 j = i + 1u; j < MBR_PARTITION_COUNT; j++) {
            const mbr_partition_t* b = &output->partitions[j];
            if (!b->present) continue;
            u64 b_start = b->first_lba;
            u64 b_end = b_start + b->sector_count;
            if (ranges_overlap(a_start, a_end, b_start, b_end))
                return reject_table(output);
        }
    }

    return true;
}
