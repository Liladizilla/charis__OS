/* installer_storage.c - one-shot, read-only primary ATA inventory. */
#include <kernel/installer_storage.h>
#include <kernel/ata.h>
#include <kernel/serial.h>
#include <kernel/printf.h>

static installer_storage_inventory_t inventory;

static void clear_bytes(void* destination, u32 length) {
    u8* bytes = (u8*)destination;
    for (u32 i = 0; i < length; i++) bytes[i] = 0;
}

static void copy_bytes(void* destination, const void* source, u32 length) {
    u8* out = (u8*)destination;
    const u8* in = (const u8*)source;
    for (u32 i = 0; i < length; i++) out[i] = in[i];
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
        inventory.status = INSTALLER_STORAGE_GPT_PROTECTIVE;
        serial_puts("[STORAGE] protective GPT MBR found; GPT entries not yet enumerated\n");
        return;
    }

    if (inventory.partition_table.scheme == PARTITION_SCHEME_MBR &&
        inventory.partition_table.valid) {
        inventory.status = INSTALLER_STORAGE_VALID_MBR;
        kprintf("[STORAGE] valid MBR: %u primary entries%s\n",
                inventory.partition_table.partition_count,
                inventory.partition_table.has_extended
                    ? "; extended chain not enumerated" : "");
        return;
    }

    inventory.status = INSTALLER_STORAGE_INVALID_PARTITION_METADATA;
    serial_puts("[STORAGE] unrecognized partition metadata rejected\n");
}

bool installer_storage_get_inventory(installer_storage_inventory_t* output) {
    if (!output) return false;
    copy_bytes(output, &inventory, (u32)sizeof(inventory));
    return inventory.status != INSTALLER_STORAGE_NOT_SCANNED;
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
        case INSTALLER_STORAGE_GPT_PROTECTIVE:
            return "Protective GPT marker found; GPT entries are not yet enumerated.";
        case INSTALLER_STORAGE_INVALID_PARTITION_METADATA:
            return "Partition metadata is malformed or outside disk bounds.";
        default:
            return "Storage inventory has not been scanned.";
    }
}
