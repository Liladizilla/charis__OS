#include <kernel/installer_storage.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static ata_device_info_t fake_device;
static bool fake_device_present;
static bool fake_read_success;
static u8 fake_sector_zero[512];
static u8 fake_gpt_header[512];
static u8 fake_gpt_entries[GPT_MAX_ARRAY_BYTES];
static u8 fake_gpt_backup_header[512];
static u8 fake_gpt_backup_entries[GPT_MAX_ARRAY_BYTES];
static bool fake_backup_header_read_success;
static bool fake_backup_array_read_success;
#define FAKE_EBR_MAX 40u
static u32 fake_ebr_lbas[FAKE_EBR_MAX];
static u8 fake_ebr_sectors[FAKE_EBR_MAX][512];
static u32 fake_ebr_count;
static u32 read_calls;
static u32 write_calls;

void* kmalloc(usize size) { return malloc((size_t)size); }
void kfree(void* pointer) { free(pointer); }

void serial_puts(const char* text) {
    (void)text;
}

void kprintf(const char* format, ...) {
    (void)format;
}

bool ata_get_device_info(ata_device_info_t* output) {
    if (!output || !fake_device_present) return false;
    *output = fake_device;
    return true;
}

bool ata_device_lba28_is_addressable(const ata_device_info_t* device, u32 lba) {
    return device && device->detected && device->io_supported &&
           (u64)lba < device->addressable_sectors &&
           (u64)lba < ATA_LBA28_SECTOR_LIMIT;
}

bool ata_read_sector(u32 lba, void* buffer) {
    if (!buffer) return false;
    read_calls++;
    if (!fake_read_success) return false;
    if (lba == 0u) memcpy(buffer, fake_sector_zero, 512u);
    else if (lba == 1u) memcpy(buffer, fake_gpt_header, 512u);
    else if (lba >= 2u && lba < 34u)
        memcpy(buffer, fake_gpt_entries + (lba - 2u) * 512u, 512u);
    else if (lba == 999999u) {
        if (!fake_backup_header_read_success) return false;
        memcpy(buffer, fake_gpt_backup_header, 512u);
    }
    else if (lba >= 999967u && lba < 999999u) {
        if (!fake_backup_array_read_success) return false;
        memcpy(buffer, fake_gpt_backup_entries +
               (lba - 999967u) * 512u, 512u);
    }
    else {
        bool found = false;
        for (u32 i = 0; i < fake_ebr_count; i++) {
            if (fake_ebr_lbas[i] == lba) {
                memcpy(buffer, fake_ebr_sectors[i], 512u);
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

bool ata_write_sector(u32 lba, const void* buffer) {
    (void)lba; (void)buffer;
    write_calls++;
    return true;
}

static int failures;

static void expect(bool condition, const char* name) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", name);
    failures++;
}

static void put_le16(u8* bytes, u16 value) {
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
}

static void put_le32(u8* bytes, u32 value) {
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
    bytes[2] = (u8)(value >> 16);
    bytes[3] = (u8)(value >> 24);
}

static void set_mbr_entry(u8* sector, u32 index, u8 type,
                          u32 first, u32 count) {
    u8* entry = &sector[446u + index * 16u];
    entry[4] = type;
    put_le32(&entry[8], first);
    put_le32(&entry[12], count);
}

static void prepare_device(void) {
    memset(&fake_device, 0, sizeof(fake_device));
    fake_device.detected = true;
    fake_device.io_supported = true;
    fake_device.logical_sector_bytes = 512u;
    fake_device.capacity_sectors = 1000000u;
    fake_device.addressable_sectors = 1000000u;
    fake_device_present = true;
    fake_read_success = true;
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    memset(fake_gpt_header, 0, sizeof(fake_gpt_header));
    memset(fake_gpt_entries, 0, sizeof(fake_gpt_entries));
    memset(fake_gpt_backup_header, 0, sizeof(fake_gpt_backup_header));
    memset(fake_gpt_backup_entries, 0, sizeof(fake_gpt_backup_entries));
    fake_backup_header_read_success = true;
    fake_backup_array_read_success = true;
    memset(fake_ebr_lbas, 0, sizeof(fake_ebr_lbas));
    memset(fake_ebr_sectors, 0, sizeof(fake_ebr_sectors));
    fake_ebr_count = 0u;
}

static u32 test_crc32(const u8* bytes, u32 length) {
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8u; bit++)
            crc = (crc >> 1) ^ ((u32)-(s32)(crc & 1u) & 0xEDB88320u);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void put_le64(u8* bytes, u64 value) {
    put_le32(bytes, (u32)value);
    put_le32(bytes + 4u, (u32)(value >> 32));
}

static void prepare_valid_gpt(void) {
    memset(fake_gpt_header, 0, 512u);
    memset(fake_gpt_entries, 0, sizeof(fake_gpt_entries));
    memcpy(fake_gpt_header, "EFI PART", 8u);
    put_le32(fake_gpt_header + 8u, 0x00010000u);
    put_le32(fake_gpt_header + 12u, 92u);
    put_le64(fake_gpt_header + 24u, 1u);
    put_le64(fake_gpt_header + 32u, 999999u);
    put_le64(fake_gpt_header + 40u, 34u);
    put_le64(fake_gpt_header + 48u, 999966u);
    for (u32 i = 0; i < 16u; i++) {
        fake_gpt_header[56u + i] = (u8)(i + 1u);
        fake_gpt_entries[i] = (u8)(0xA0u + i);
        fake_gpt_entries[16u + i] = (u8)(0x10u + i);
    }
    put_le64(fake_gpt_header + 72u, 2u);
    put_le32(fake_gpt_header + 80u, GPT_MAX_PARTITIONS);
    put_le32(fake_gpt_header + 84u, 128u);
    put_le64(fake_gpt_entries + 32u, 2048u);
    put_le64(fake_gpt_entries + 40u, 10000u);
    put_le32(fake_gpt_header + 88u, test_crc32(fake_gpt_entries, GPT_MIN_ARRAY_BYTES));
    put_le32(fake_gpt_header + 16u, test_crc32(fake_gpt_header, 92u));

    memcpy(fake_gpt_backup_entries, fake_gpt_entries, GPT_MIN_ARRAY_BYTES);
    memcpy(fake_gpt_backup_header, "EFI PART", 8u);
    put_le32(fake_gpt_backup_header + 8u, 0x00010000u);
    put_le32(fake_gpt_backup_header + 12u, 92u);
    put_le64(fake_gpt_backup_header + 24u, 999999u);
    put_le64(fake_gpt_backup_header + 32u, 1u);
    put_le64(fake_gpt_backup_header + 40u, 34u);
    put_le64(fake_gpt_backup_header + 48u, 999966u);
    for (u32 i = 0; i < 16u; i++)
        fake_gpt_backup_header[56u + i] = (u8)(i + 1u);
    put_le64(fake_gpt_backup_header + 72u, 999967u);
    put_le32(fake_gpt_backup_header + 80u, GPT_MAX_PARTITIONS);
    put_le32(fake_gpt_backup_header + 84u, 128u);
    put_le32(fake_gpt_backup_header + 88u,
             test_crc32(fake_gpt_backup_entries, GPT_MIN_ARRAY_BYTES));
    put_le32(fake_gpt_backup_header + 16u,
             test_crc32(fake_gpt_backup_header, 92u));
}

static void prepare_gpt_disk(void) {
    prepare_device();
    prepare_valid_gpt();
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
}

static void prepare_ebr_record(u8 sector[512],
                                u8 logical_type, u32 logical_relative,
                                u32 logical_count, u8 link_type,
                                u32 link_relative, u32 link_count) {
    memset(sector, 0, 512u);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    set_mbr_entry(sector, 0, logical_type, logical_relative, logical_count);
    set_mbr_entry(sector, 1, link_type, link_relative, link_count);
}

static void prepare_extended_mbr(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0x0Fu, 10000u, 500000u);

    fake_ebr_count = 2u;
    fake_ebr_lbas[0] = 10000u;
    fake_ebr_lbas[1] = 110000u;
    prepare_ebr_record(fake_ebr_sectors[0],
                       0x83u, 63u, 50000u,
                       0x0Fu, 100000u, 400000u);
    prepare_ebr_record(fake_ebr_sectors[1],
                       0x07u, 63u, 50000u,
                       0u, 0u, 0u);
}

static void prepare_cyclic_ebr_chain(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0x0Fu, 10000u, 500000u);

    fake_ebr_count = 3u;
    fake_ebr_lbas[0] = 10000u;
    fake_ebr_lbas[1] = 110000u;
    fake_ebr_lbas[2] = 120000u;
    prepare_ebr_record(fake_ebr_sectors[0],
                       0x83u, 63u, 5000u,
                       0x0Fu, 100000u, 200000u);
    prepare_ebr_record(fake_ebr_sectors[1],
                       0x07u, 140000u, 5000u,
                       0x0Fu, 110000u, 80000u);
    prepare_ebr_record(fake_ebr_sectors[2],
                       0x83u, 10000u, 5000u,
                       0x0Fu, 100000u, 10000u);
}

static void prepare_ebr_link_overlapping_prior_partition(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0x0Fu, 10000u, 500000u);

    fake_ebr_count = 2u;
    fake_ebr_lbas[0] = 10000u;
    fake_ebr_lbas[1] = 110000u;
    prepare_ebr_record(fake_ebr_sectors[0],
                       0x83u, 63u, 5000u,
                       0x0Fu, 100000u, 300000u);
    prepare_ebr_record(fake_ebr_sectors[1],
                       0x07u, 140000u, 5000u,
                       0x0Fu, 1000u, 9000u);
}

static void prepare_maximum_ebr_chain(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0x0Fu, 10000u, 500000u);

    fake_ebr_count = MBR_MAX_LOGICAL_PARTITIONS;
    for (u32 i = 0; i < fake_ebr_count; i++) {
        u32 offset = (i + 1u) * 1000u;
        fake_ebr_lbas[i] = 10000u + i * 1000u;
        prepare_ebr_record(fake_ebr_sectors[i],
                           0x83u, 63u, 500u,
                           0x0Fu, offset, 500000u - offset);
    }
}

static void prepare_fat32_boot_sector(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[0] = 0xEB;
    fake_sector_zero[2] = 0x90;
    put_le16(&fake_sector_zero[11], 512u);
    fake_sector_zero[13] = 8u;
    put_le16(&fake_sector_zero[14], 32u);
    fake_sector_zero[16] = 2u;
    put_le32(&fake_sector_zero[32], 1000000u);
    put_le32(&fake_sector_zero[36], 4096u);
    put_le32(&fake_sector_zero[44], 2u);
    memcpy(&fake_sector_zero[82], "FAT32   ", 8);
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
}

static void prepare_mbr(void) {
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0x0Cu, 2048u, 500000u);
}

int main(void) {
    const installer_storage_inventory_t* inventory;
    read_calls = 0;
    write_calls = 0;

    prepare_device();
    fake_device_present = false;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_NO_DEVICE,
           "no ATA device is reported distinctly");

    prepare_device();
    fake_device.io_supported = false;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->device_detected &&
           inventory->status == INSTALLER_STORAGE_DEVICE_UNREADABLE,
           "detected but unsupported device is not read");

    prepare_device();
    fake_read_success = false;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_SECTOR_READ_ERROR,
           "sector-zero read failures are reported");

    prepare_device();
    prepare_fat32_boot_sector();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->sector_zero_read &&
           inventory->status == INSTALLER_STORAGE_FAT32_AT_LBA0,
           "whole-volume FAT32 boot sector is not misclassified as MBR");

    prepare_device();
    prepare_mbr();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_VALID_MBR &&
           inventory->partition_table.partition_count == 1u,
           "valid MBR is connected to the startup inventory");

    prepare_device();
    prepare_extended_mbr();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_VALID_MBR &&
           inventory->partition_table.logical_partition_count == 2u &&
           inventory->partition_table.fully_enumerated &&
           inventory->partition_table.logical_partitions[0].first_lba == 10063u &&
           inventory->partition_table.logical_partitions[1].first_lba == 110063u,
           "read-only scan follows and enumerates a valid two-record EBR chain");

    prepare_device();
    prepare_extended_mbr();
    fake_device.addressable_sectors = 10000u;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_EBR_METADATA_UNREADABLE &&
           !inventory->partition_table.fully_enumerated,
           "EBR metadata outside the current read window remains incomplete");

    prepare_device();
    prepare_extended_mbr();
    prepare_ebr_record(fake_ebr_sectors[0],
                       0x83u, 63u, 120000u,
                       0x0Fu, 100000u, 400000u);
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_EBR_CHAIN &&
           inventory->partition_table.logical_partition_count == 0u,
           "an EBR link inside its own logical data extent is rejected");

    prepare_device();
    prepare_extended_mbr();
    fake_ebr_count = 1u; /* Second EBR is intentionally missing. */
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_EBR_METADATA_UNREADABLE &&
           inventory->partition_table.logical_partition_count == 1u &&
           !inventory->partition_table.fully_enumerated,
           "unreadable later EBR preserves the partial logical inventory");

    prepare_device();
    prepare_ebr_link_overlapping_prior_partition();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_EBR_CHAIN &&
           inventory->partition_table.logical_partition_count == 2u &&
           !inventory->partition_table.fully_enumerated,
           "next EBR link extent cannot overlap an earlier logical partition");

    prepare_device();
    prepare_cyclic_ebr_chain();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_EBR_CHAIN &&
           inventory->partition_table.logical_partition_count == 3u &&
           !inventory->partition_table.fully_enumerated,
           "multi-record EBR cycle is caught by the visited-LBA set");

    prepare_device();
    prepare_maximum_ebr_chain();
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_EBR_CHAIN_LIMIT &&
           inventory->partition_table.logical_partition_count ==
               MBR_MAX_LOGICAL_PARTITIONS &&
           !inventory->partition_table.fully_enumerated,
           "EBR traversal stops at the fixed 32-record limit");

    prepare_device();
    prepare_extended_mbr();
    /* The second EBR links to itself. The cycle is rejected before another read. */
    set_mbr_entry(fake_ebr_sectors[1], 1, 0x0Fu, 100000u, 400000u);
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_EBR_CHAIN &&
           !inventory->partition_table.fully_enumerated,
           "cyclic EBR link is rejected without claiming complete inventory");

    prepare_device();
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    fake_device.addressable_sectors = 1u;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_GPT_METADATA_UNREADABLE,
           "protective GPT marker is distinguished when LBA1 is inaccessible");


    prepare_device();
    prepare_valid_gpt();
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_VALID_GPT &&
           inventory->gpt_table.valid &&
           inventory->gpt_table.backup_verified &&
           inventory->gpt_table.partition_count == 1u,
           "protective MBR leads to primary and backup GPT CRC validation");

    prepare_gpt_disk();
    fake_gpt_backup_header[16] ^= 1u;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_GPT_BACKUP &&
           inventory->gpt_table.valid && !inventory->gpt_table.backup_verified,
           "backup GPT header CRC corruption is reported without discarding valid primary data");

    prepare_gpt_disk();
    fake_gpt_backup_entries[200] ^= 1u;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_GPT_BACKUP &&
           inventory->gpt_table.valid && !inventory->gpt_table.backup_verified,
           "backup entry-array mismatch is rejected while primary inventory remains available");

    prepare_gpt_disk();
    fake_device.addressable_sectors = 999999u;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_GPT_BACKUP_UNREADABLE &&
           inventory->gpt_table.valid && !inventory->gpt_table.backup_verified,
           "primary GPT remains available when backup header is outside the LBA28 window");

    prepare_gpt_disk();
    fake_backup_array_read_success = false;
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_GPT_BACKUP_UNREADABLE &&
           inventory->gpt_table.valid && !inventory->gpt_table.backup_verified,
           "backup entry-array read failure is explicit and non-destructive");

    prepare_device();
    prepare_valid_gpt();
    fake_gpt_entries[200] ^= 1u;
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_PARTITION_METADATA,
           "GPT array CRC corruption is rejected during real inventory scan");

    prepare_device();
    prepare_mbr();
    set_mbr_entry(fake_sector_zero, 1, 0x07u, 10000u, 500000u);
    installer_storage_init();
    expect((inventory = installer_storage_get()) != NULL &&
           inventory->status == INSTALLER_STORAGE_INVALID_PARTITION_METADATA,
           "malformed overlapping primary partitions are rejected");

    expect(write_calls == 0u,
           "storage discovery never invokes the disk write API");
    printf("Installer storage sector reads: %u\\n", read_calls);
    expect(read_calls == 330u,
           "only bounded MBR, GPT and EBR metadata sectors are read");
    expect(strcmp(installer_storage_status_label(
               INSTALLER_STORAGE_VALID_MBR),
               "Valid legacy MBR partition table found.") == 0,
           "inventory status labels are stable");

    if (failures) {
        fprintf(stderr, "Installer storage: %d failure(s)\n", failures);
        return 1;
    }
    puts("Installer storage: all checks passed");
    return 0;
}
