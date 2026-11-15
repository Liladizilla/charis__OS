/* partition.c - defensive read-only MBR and GPT metadata parsing. */
#include <kernel/partition.h>

static void clear_bytes(void* destination, u32 length) {
    u8* bytes = (u8*)destination;
    for (u32 i = 0; i < length; i++) bytes[i] = 0;
}

static u32 read_le32(const u8* bytes) {
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static u64 read_le64(const u8* bytes) {
    return (u64)read_le32(bytes) |
           ((u64)read_le32(bytes + 4u) << 32);
}

static bool all_zero(const u8* bytes, u32 length) {
    for (u32 i = 0; i < length; i++)
        if (bytes[i]) return false;
    return true;
}

static bool bytes_equal(const u8* a, const u8* b, u32 length) {
    for (u32 i = 0; i < length; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool ranges_overlap(u64 first_a, u64 end_a,
                           u64 first_b, u64 end_b) {
    return first_a < end_b && first_b < end_a;
}

static u32 crc32_update(u32 crc, const u8* bytes, u32 length) {
    for (u32 i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8u; bit++) {
            u32 mask = (u32)-(s32)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static u32 crc32_bytes(const u8* bytes, u32 length) {
    return crc32_update(0xFFFFFFFFu, bytes, length) ^ 0xFFFFFFFFu;
}

static u32 crc32_gpt_header(const u8* bytes, u32 length) {
    u32 crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, bytes, 16u);
    const u8 zero_crc[4] = {0, 0, 0, 0};
    crc = crc32_update(crc, zero_crc, sizeof(zero_crc));
    crc = crc32_update(crc, bytes + 20u, length - 20u);
    return crc ^ 0xFFFFFFFFu;
}

static bool is_extended_type(u8 type) {
    return type == 0x05u || type == 0x0Fu || type == 0x85u;
}

static bool reject_mbr(partition_table_t* output) {
    output->scheme = PARTITION_SCHEME_INVALID;
    output->valid = false;
    output->fully_enumerated = false;
    return false;
}

bool partition_parse_mbr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                partition_table_t* output) {
    if (!sector || !device || !output) return false;
    clear_bytes(output, (u32)sizeof(*output));
    output->scheme = PARTITION_SCHEME_NONE;

    if (!device->detected || !device->capacity_sectors ||
        device->logical_sector_bytes != MBR_SECTOR_BYTES) {
        output->scheme = PARTITION_SCHEME_INVALID;
        return false;
    }

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
            if (boot_flag || first_lba || sector_count)
                return reject_mbr(output);
            continue;
        }

        if ((boot_flag != 0u && boot_flag != 0x80u) ||
            first_lba == 0u || sector_count == 0u)
            return reject_mbr(output);

        u64 end_lba = (u64)first_lba + (u64)sector_count;
        if (end_lba <= first_lba || end_lba > device->capacity_sectors)
            return reject_mbr(output);

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
            output->extended_base_lba = first_lba;
            output->extended_end_lba = end_lba;
            extended_count++;
        }
        if (type == 0xEEu) protective_count++;
    }

    if (extended_count > 1u) return reject_mbr(output);

    if (protective_count) {
        if (protective_count != 1u || output->partition_count != 1u)
            return reject_mbr(output);

        const mbr_partition_t* protective = NULL;
        for (u32 i = 0; i < MBR_PARTITION_COUNT; i++) {
            if (output->partitions[i].present) {
                protective = &output->partitions[i];
                break;
            }
        }
        if (!protective || protective->first_lba != 1u)
            return reject_mbr(output);

        output->scheme = PARTITION_SCHEME_GPT_PROTECTIVE;
        output->fully_enumerated = false;
    }

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
                return reject_mbr(output);
        }
    }

    return true;
}

/* Decode one EBR without following links. The caller owns the bounded
 * traversal, visited-LBA set and disk reads. MBR logical-partition offsets are
 * relative to this EBR; the next-EBR link is relative to the extended base. */
bool partition_parse_ebr_sector(const u8 sector[MBR_SECTOR_BYTES],
                                u64 ebr_lba,
                                u64 extended_base_lba,
                                u64 extended_end_lba,
                                const ata_device_info_t* device,
                                ebr_sector_info_t* output) {
    if (!sector || !device || !output) return false;
    clear_bytes(output, (u32)sizeof(*output));

    if (!device->detected || !device->capacity_sectors ||
        device->logical_sector_bytes != MBR_SECTOR_BYTES ||
        extended_base_lba >= extended_end_lba ||
        extended_end_lba > device->capacity_sectors ||
        ebr_lba < extended_base_lba || ebr_lba >= extended_end_lba)
        return false;

    if (sector[510] != 0x55u || sector[511] != 0xAAu)
        return false;

    const u8* logical_raw = &sector[446u];
    const u8* link_raw = &sector[462u];
    const u8* reserved_a = &sector[478u];
    const u8* reserved_b = &sector[494u];

    /* Only the first two EBR entries are defined. */
    if (!all_zero(reserved_a, 16u) || !all_zero(reserved_b, 16u))
        return false;

    u8 logical_type = logical_raw[4];
    u32 logical_relative = read_le32(&logical_raw[8]);
    u32 logical_count = read_le32(&logical_raw[12]);
    u8 logical_boot = logical_raw[0];

    if (!logical_type || is_extended_type(logical_type) ||
        logical_type == 0xEEu ||
        (logical_boot != 0u && logical_boot != 0x80u) ||
        !logical_relative || !logical_count)
        return false;

    u64 logical_first = ebr_lba + (u64)logical_relative;
    u64 logical_end = logical_first + (u64)logical_count;
    if (logical_first <= ebr_lba || logical_first > 0xFFFFFFFFULL ||
        logical_end <= logical_first || logical_first < extended_base_lba ||
        logical_end > extended_end_lba ||
        logical_end > device->capacity_sectors)
        return false;

    output->logical_partition.present = true;
    output->logical_partition.bootable = logical_boot == 0x80u;
    output->logical_partition.type = logical_type;
    output->logical_partition.first_lba = (u32)logical_first;
    output->logical_partition.sector_count = logical_count;
    output->logical_partition.accessible =
        logical_end <= device->addressable_sectors;

    u8 link_type = link_raw[4];
    if (!link_type) {
        if (!all_zero(link_raw, 16u)) {
            clear_bytes(output, (u32)sizeof(*output));
            return false;
        }
        return true;
    }

    u8 link_boot = link_raw[0];
    u32 link_relative = read_le32(&link_raw[8]);
    u32 link_count = read_le32(&link_raw[12]);
    if (!is_extended_type(link_type) || link_boot != 0u ||
        !link_relative || !link_count) {
        clear_bytes(output, (u32)sizeof(*output));
        return false;
    }

    u64 next_ebr = extended_base_lba + (u64)link_relative;
    u64 link_end = next_ebr + (u64)link_count;
    if (next_ebr < extended_base_lba || next_ebr >= extended_end_lba ||
        next_ebr > 0xFFFFFFFFULL || next_ebr == ebr_lba ||
        link_end <= next_ebr ||
        link_end > extended_end_lba || link_end > device->capacity_sectors ||
        (next_ebr >= logical_first && next_ebr < logical_end)) {
        clear_bytes(output, (u32)sizeof(*output));
        return false;
    }

    output->has_next_ebr = true;
    output->next_ebr_lba = next_ebr;
    return true;
}

bool partition_parse_gpt_header(const u8 sector[MBR_SECTOR_BYTES],
                                const ata_device_info_t* device,
                                gpt_header_info_t* output) {
    if (!sector || !device || !output) return false;
    clear_bytes(output, (u32)sizeof(*output));

    static const u8 signature[8] = {'E','F','I',' ','P','A','R','T'};
    if (!bytes_equal(sector, signature, sizeof(signature))) return false;

    u32 revision = read_le32(&sector[8]);
    u32 header_size = read_le32(&sector[12]);
    u32 header_crc = read_le32(&sector[16]);
    u32 reserved = read_le32(&sector[20]);
    u64 current_lba = read_le64(&sector[24]);
    u64 backup_lba = read_le64(&sector[32]);
    u64 first_usable = read_le64(&sector[40]);
    u64 last_usable = read_le64(&sector[48]);
    u64 entry_array_lba = read_le64(&sector[72]);
    u32 entry_count = read_le32(&sector[80]);
    u32 entry_size = read_le32(&sector[84]);
    u32 entry_crc = read_le32(&sector[88]);

    if (!device->detected || !device->capacity_sectors ||
        device->logical_sector_bytes != MBR_SECTOR_BYTES)
        return false;

    if (revision != 0x00010000u ||
        header_size < GPT_HEADER_MIN_SIZE ||
        header_size > MBR_SECTOR_BYTES ||
        reserved != 0u || current_lba != 1u ||
        backup_lba >= device->capacity_sectors || backup_lba == current_lba ||
        first_usable < 2u || first_usable > last_usable ||
        last_usable >= device->capacity_sectors ||
        backup_lba <= last_usable ||
        entry_array_lba < 2u ||
        entry_count == 0u || entry_count > GPT_MAX_PARTITIONS ||
        entry_size < 128u || entry_size > GPT_MAX_ENTRY_SIZE ||
        (entry_size & 7u) != 0u)
        return false;

    if (crc32_gpt_header(sector, header_size) != header_crc)
        return false;

    u64 array_bytes_wide = (u64)entry_count * (u64)entry_size;
    if (!array_bytes_wide || array_bytes_wide > GPT_MAX_ARRAY_BYTES)
        return false;

    u64 array_sectors_wide = (array_bytes_wide + 511u) / 512u;
    u64 array_end = entry_array_lba + array_sectors_wide;
    if (array_end <= entry_array_lba ||
        array_end > device->capacity_sectors ||
        array_end > first_usable ||
        (backup_lba >= entry_array_lba && backup_lba < array_end))
        return false;

    output->backup_lba = backup_lba;
    output->first_usable_lba = first_usable;
    output->last_usable_lba = last_usable;
    output->entry_array_lba = entry_array_lba;
    output->entry_count = entry_count;
    output->entry_size = entry_size;
    output->entry_array_bytes = (u32)array_bytes_wide;
    output->entry_array_sectors = (u32)array_sectors_wide;
    output->entry_array_crc32 = entry_crc;
    for (u32 i = 0; i < 16u; i++) output->disk_guid[i] = sector[56u + i];

    if (all_zero(output->disk_guid, 16u)) {
        clear_bytes(output, (u32)sizeof(*output));
        return false;
    }

    return true;
}

bool partition_parse_gpt_entries(const gpt_header_info_t* header,
                                 const u8* entry_array,
                                 u32 entry_array_bytes,
                                 const ata_device_info_t* device,
                                 gpt_partition_table_t* output) {
    if (!header || !entry_array || !device || !output) return false;
    clear_bytes(output, (u32)sizeof(*output));

    if (!device->detected || !device->capacity_sectors ||
        header->entry_count == 0u || header->entry_count > GPT_MAX_PARTITIONS ||
        header->entry_size < 128u || header->entry_size > GPT_MAX_ENTRY_SIZE ||
        (header->entry_size & 7u) != 0u)
        return false;

    u64 required_bytes = (u64)header->entry_count * header->entry_size;
    if (required_bytes > GPT_MAX_ARRAY_BYTES ||
        required_bytes > entry_array_bytes ||
        required_bytes != header->entry_array_bytes)
        return false;

    if (crc32_bytes(entry_array, (u32)required_bytes) !=
        header->entry_array_crc32)
        return false;

    output->entry_count = header->entry_count;
    output->first_usable_lba = header->first_usable_lba;
    output->last_usable_lba = header->last_usable_lba;
    for (u32 i = 0; i < 16u; i++) output->disk_guid[i] = header->disk_guid[i];
    output->fully_enumerated = true;

    for (u32 i = 0; i < header->entry_count; i++) {
        const u8* raw = entry_array + (u64)i * header->entry_size;
        if (all_zero(raw, 16u)) continue;

        gpt_partition_t* entry = &output->partitions[output->partition_count];
        for (u32 byte = 0; byte < 16u; byte++) {
            entry->type_guid[byte] = raw[byte];
            entry->unique_guid[byte] = raw[16u + byte];
        }

        u64 first_lba = read_le64(raw + 32u);
        u64 last_lba = read_le64(raw + 40u);
        u64 attributes = read_le64(raw + 48u);

        if (all_zero(entry->unique_guid, 16u) ||
            first_lba < header->first_usable_lba ||
            first_lba > last_lba ||
            last_lba > header->last_usable_lba ||
            last_lba >= device->capacity_sectors) {
            clear_bytes(output, (u32)sizeof(*output));
            return false;
        }

        entry->present = true;
        entry->first_lba = first_lba;
        entry->last_lba = last_lba;
        entry->attributes = attributes;
        entry->accessible = last_lba < device->addressable_sectors;

        for (u32 prior = 0; prior < output->partition_count; prior++) {
            const gpt_partition_t* other = &output->partitions[prior];
            if (ranges_overlap(entry->first_lba, entry->last_lba + 1u,
                               other->first_lba, other->last_lba + 1u) ||
                bytes_equal(entry->unique_guid, other->unique_guid, 16u)) {
                clear_bytes(output, (u32)sizeof(*output));
                return false;
            }
        }

        output->partition_count++;
    }

    output->valid = true;
    return true;
}
