/* installer_storage.c - one-shot, read-only primary ATA inventory. */
#include <kernel/installer_storage.h>
#include <kernel/ata.h>
#include <kernel/serial.h>
#include <kernel/printf.h>
#include <kernel/memory.h>

static installer_storage_inventory_t inventory;

static void clear_bytes(void* destination, u32 length) {
    u8* bytes = (u8*)destination;
    for (u32 i = 0; i < length; i++) bytes[i] = 0;
}

static u16 read_le16(const u8* bytes) {
    return (u16)((u16)bytes[0] | ((u16)bytes[1] << 8));
}

static u32 read_le32(const u8* bytes) {
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static bool is_fat32_volume_boot_sector(const u8 sector[512],
                                        const ata_device_info_t* device) {
    u16 bytes_per_sector = read_le16(&sector[11]);
    u8 sectors_per_cluster = sector[13];
    u16 reserved_sectors = read_le16(&sector[14]);
    u8 fat_count = sector[16];
    u32 total_sectors = read_le16(&sector[19]);
    if (!total_sectors) total_sectors = read_le32(&sector[32]);

    u32 fat_size = read_le16(&sector[22]);
    if (!fat_size) fat_size = read_le32(&sector[36]);

    u32 root_cluster = read_le32(&sector[44]);
    bool cluster_size_valid = sectors_per_cluster != 0u &&
        sectors_per_cluster <= 128u &&
        (sectors_per_cluster & (sectors_per_cluster - 1u)) == 0u;

    if (bytes_per_sector != 512u || !cluster_size_valid ||
        !reserved_sectors || !fat_count || fat_count > 2u ||
        !fat_size || !total_sectors || root_cluster < 2u ||
        (u64)total_sectors > device->capacity_sectors)
        return false;

    if (sector[510] != 0x55u || sector[511] != 0xAAu)
        return false;

    /* Match the FAT32 filesystem-type label used by the existing VFS mount
     * path as an additional clue, not as a substitute for BPB validation. */
    static const char fat32_label[5] = { 'F', 'A', 'T', '3', '2' };
    for (u32 i = 0; i < sizeof(fat32_label); i++) {
        if (sector[82u + i] != (u8)fat32_label[i]) return false;
    }

    return true;
}

static bool partition_ranges_overlap(u64 first_a, u64 end_a,
                                       u64 first_b, u64 end_b) {
    return first_a < end_b && first_b < end_a;
}

static bool lba_inside_partition(u64 lba,
                                 const mbr_partition_t* partition) {
    if (!partition || !partition->present) return false;
    u64 start = partition->first_lba;
    u64 end = start + partition->sector_count;
    return lba >= start && lba < end;
}

/* Traverse at most MBR_MAX_LOGICAL_PARTITIONS EBRs. Each link is checked
 * against the extended container, the addressable I/O range, prior EBRs and
 * every discovered partition before it is followed. */
static installer_storage_status_t scan_extended_chain(void) {
    partition_table_t* table = &inventory.partition_table;
    if (!table->has_extended) {
        table->fully_enumerated = true;
        return INSTALLER_STORAGE_VALID_MBR;
    }

    u64 visited[MBR_MAX_LOGICAL_PARTITIONS];
    u32 visited_count = 0;
    u64 current_ebr = table->extended_base_lba;
    u64 container_start = table->extended_base_lba;
    u64 container_end = table->extended_end_lba;

    for (u32 hop = 0; hop < MBR_MAX_LOGICAL_PARTITIONS; hop++) {
        if (current_ebr < container_start || current_ebr >= container_end ||
            current_ebr >= inventory.device.addressable_sectors ||
            current_ebr >= ATA_LBA28_SECTOR_LIMIT ||
            current_ebr > 0xFFFFFFFFULL)
            return INSTALLER_STORAGE_EBR_METADATA_UNREADABLE;

        for (u32 i = 0; i < visited_count; i++) {
            if (visited[i] == current_ebr)
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }
        for (u32 i = 0; i < table->logical_partition_count; i++) {
            if (lba_inside_partition(current_ebr,
                                     &table->logical_partitions[i]))
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }

        visited[visited_count++] = current_ebr;

        u8 sector[MBR_SECTOR_BYTES];
        if (!ata_read_sector((u32)current_ebr, sector))
            return INSTALLER_STORAGE_EBR_METADATA_UNREADABLE;

        ebr_sector_info_t decoded;
        if (!partition_parse_ebr_sector(sector, current_ebr,
                container_start, container_end, &inventory.device, &decoded))
            return INSTALLER_STORAGE_INVALID_EBR_CHAIN;

        const mbr_partition_t* logical = &decoded.logical_partition;
        u64 logical_start = logical->first_lba;
        u64 logical_end = logical_start + logical->sector_count;

        for (u32 i = 0; i < table->partition_count; i++) {
            const mbr_partition_t* primary = &table->partitions[i];
            if (!primary->present || primary->extended) continue;
            u64 primary_start = primary->first_lba;
            u64 primary_end = primary_start + primary->sector_count;
            if (partition_ranges_overlap(logical_start, logical_end,
                                         primary_start, primary_end))
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }

        for (u32 i = 0; i < table->logical_partition_count; i++) {
            const mbr_partition_t* prior = &table->logical_partitions[i];
            u64 prior_start = prior->first_lba;
            u64 prior_end = prior_start + prior->sector_count;
            if (partition_ranges_overlap(logical_start, logical_end,
                                         prior_start, prior_end))
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }

        for (u32 i = 0; i < visited_count; i++) {
            if (visited[i] >= logical_start && visited[i] < logical_end)
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }

        table->logical_partitions[table->logical_partition_count++] = *logical;

        if (!decoded.has_next_ebr) {
            table->fully_enumerated = true;
            return INSTALLER_STORAGE_VALID_MBR;
        }

        for (u32 i = 0; i < table->logical_partition_count; i++) {
            if (lba_inside_partition(decoded.next_ebr_lba,
                                     &table->logical_partitions[i]))
                return INSTALLER_STORAGE_INVALID_EBR_CHAIN;
        }

        if (hop + 1u == MBR_MAX_LOGICAL_PARTITIONS)
            return INSTALLER_STORAGE_EBR_CHAIN_LIMIT;
        current_ebr = decoded.next_ebr_lba;
    }

    return INSTALLER_STORAGE_EBR_CHAIN_LIMIT;
}

void installer_storage_init(void) {
    clear_bytes(&inventory, (u32)sizeof(inventory));

    if (!ata_get_device_info(&inventory.device)) {
        inventory.status = INSTALLER_STORAGE_NO_DEVICE;
        serial_puts("[STORAGE] no primary-master ATA device identified\n");
        return;
    }

    inventory.device_detected = true;

    if (!ata_device_lba28_is_addressable(&inventory.device, 0u)) {
        inventory.status = INSTALLER_STORAGE_DEVICE_UNREADABLE;
        serial_puts("[STORAGE] device identified but LBA0 is not readable by current driver\n");
        return;
    }

    u8 sector_zero[512];
    if (!ata_read_sector(0u, sector_zero)) {
        inventory.status = INSTALLER_STORAGE_SECTOR_READ_ERROR;
        serial_puts("[STORAGE] primary-master sector zero read failed\n");
        return;
    }

    inventory.sector_zero_read = true;

    if (is_fat32_volume_boot_sector(sector_zero, &inventory.device)) {
        inventory.status = INSTALLER_STORAGE_FAT32_AT_LBA0;
        serial_puts("[STORAGE] FAT32 volume boot sector found directly at LBA0\n");
        return;
    }

    if (sector_zero[510] != 0x55u || sector_zero[511] != 0xAAu) {
        inventory.status = INSTALLER_STORAGE_NO_PARTITION_SIGNATURE;
        serial_puts("[STORAGE] no recognized MBR signature at LBA0\n");
        return;
    }

    if (!partition_parse_mbr_sector(sector_zero, &inventory.device,
                                    &inventory.partition_table)) {
        inventory.status = INSTALLER_STORAGE_INVALID_PARTITION_METADATA;
        serial_puts("[STORAGE] malformed or unsafe MBR metadata rejected\n");
        return;
    }

    if (inventory.partition_table.scheme == PARTITION_SCHEME_GPT_PROTECTIVE) {
        u8 gpt_sector[512];
        if (!ata_device_lba28_is_addressable(&inventory.device, 1u) ||
            !ata_read_sector(1u, gpt_sector)) {
            inventory.status = INSTALLER_STORAGE_GPT_METADATA_UNREADABLE;
            serial_puts("[STORAGE] protective GPT marker found, but primary GPT header cannot be read\n");
            return;
        }

        if (!partition_parse_gpt_header(gpt_sector, &inventory.device,
                                        &inventory.gpt_header)) {
            inventory.status = INSTALLER_STORAGE_INVALID_PARTITION_METADATA;
            serial_puts("[STORAGE] primary GPT header signature/CRC/geometry is invalid\n");
            return;
        }

        u64 array_end = inventory.gpt_header.entry_array_lba +
                        inventory.gpt_header.entry_array_sectors;
        if (array_end <= inventory.gpt_header.entry_array_lba ||
            array_end > inventory.device.addressable_sectors ||
            inventory.gpt_header.entry_array_lba >= ATA_LBA28_SECTOR_LIMIT) {
            inventory.status = INSTALLER_STORAGE_GPT_METADATA_UNREADABLE;
            serial_puts("[STORAGE] GPT entry array lies outside the current LBA28 read window\n");
            return;
        }

        usize padded_bytes = (usize)inventory.gpt_header.entry_array_sectors * 512u;
        u8* entry_array = (u8*)kmalloc(padded_bytes);
        if (!entry_array) {
            inventory.status = INSTALLER_STORAGE_GPT_METADATA_UNREADABLE;
            serial_puts("[STORAGE] insufficient kernel heap for bounded GPT entry array\n");
            return;
        }

        bool array_read = true;
        for (u32 sector_index = 0;
             sector_index < inventory.gpt_header.entry_array_sectors;
             sector_index++) {
            u64 lba_wide = inventory.gpt_header.entry_array_lba + sector_index;
            if (lba_wide > 0xFFFFFFFFULL ||
                !ata_device_lba28_is_addressable(&inventory.device,
                                                (u32)lba_wide) ||
                !ata_read_sector((u32)lba_wide,
                                 entry_array + sector_index * 512u)) {
                array_read = false;
                break;
            }
        }

        if (!array_read) {
            kfree(entry_array);
            inventory.status = INSTALLER_STORAGE_GPT_METADATA_UNREADABLE;
            serial_puts("[STORAGE] one or more GPT entry-array sectors could not be read\n");
            return;
        }

        bool parsed = partition_parse_gpt_entries(&inventory.gpt_header,
            entry_array, inventory.gpt_header.entry_array_bytes,
            &inventory.device, &inventory.gpt_table);
        kfree(entry_array);

        if (!parsed) {
            inventory.status = INSTALLER_STORAGE_INVALID_PARTITION_METADATA;
            serial_puts("[STORAGE] GPT entry-array CRC or partition geometry is invalid\n");
            return;
        }

        inventory.status = INSTALLER_STORAGE_VALID_GPT;
        kprintf("[STORAGE] valid GPT: %u of %u bounded entries contain partitions\n",
                inventory.gpt_table.partition_count,
                inventory.gpt_table.entry_count);
        return;
    }

    if (inventory.partition_table.scheme == PARTITION_SCHEME_MBR &&
        inventory.partition_table.valid) {
        inventory.status = scan_extended_chain();
        if (inventory.status == INSTALLER_STORAGE_VALID_MBR) {
            kprintf("[STORAGE] valid MBR: %u primary, %u logical partitions%s\n",
                    inventory.partition_table.partition_count,
                    inventory.partition_table.logical_partition_count,
                    inventory.partition_table.has_extended &&
                    !inventory.partition_table.fully_enumerated
                        ? "; extended chain incomplete" : "");
        } else if (inventory.status == INSTALLER_STORAGE_EBR_METADATA_UNREADABLE) {
            serial_puts("[STORAGE] EBR chain leaves the readable LBA28 window or a sector read failed\n");
        } else if (inventory.status == INSTALLER_STORAGE_EBR_CHAIN_LIMIT) {
            serial_puts("[STORAGE] EBR chain exceeds the bounded logical-partition limit\n");
        } else {
            serial_puts("[STORAGE] malformed, cyclic or overlapping EBR chain rejected\n");
        }
        return;
    }

    inventory.status = INSTALLER_STORAGE_INVALID_PARTITION_METADATA;
    serial_puts("[STORAGE] unrecognized partition metadata rejected\n");
}

const installer_storage_inventory_t* installer_storage_get(void) {
    return inventory.status == INSTALLER_STORAGE_NOT_SCANNED ? NULL : &inventory;
}

const char* installer_storage_status_label(installer_storage_status_t status) {
    switch (status) {
        case INSTALLER_STORAGE_NO_DEVICE:
            return "No primary-master ATA device was identified.";
        case INSTALLER_STORAGE_DEVICE_UNREADABLE:
            return "Device detected, but sector zero is inaccessible.";
        case INSTALLER_STORAGE_SECTOR_READ_ERROR:
            return "Device identified, but sector-zero read failed.";
        case INSTALLER_STORAGE_FAT32_AT_LBA0:
            return "Existing FAT32 volume found directly at LBA 0.";
        case INSTALLER_STORAGE_NO_PARTITION_SIGNATURE:
            return "No recognized partition signature at LBA 0.";
        case INSTALLER_STORAGE_VALID_MBR:
            return "Valid legacy MBR partition table found.";
        case INSTALLER_STORAGE_VALID_GPT:
            return "Valid GPT header and partition array passed CRC and range validation.";
        case INSTALLER_STORAGE_GPT_METADATA_UNREADABLE:
            return "GPT metadata is outside the current read window or could not be read.";
        case INSTALLER_STORAGE_INVALID_PARTITION_METADATA:
            return "Partition metadata is malformed or outside disk bounds.";
        default:
            return "Storage inventory has not been scanned.";
    }
}
