#include <kernel/installer_storage.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static ata_device_info_t fake_device;
static bool fake_device_present;
static bool fake_read_success;
static u8 fake_sector_zero[512];
static u8 fake_gpt_header[512];
static u8 fake_gpt_entries[512];
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
    else if (lba == 2u) memcpy(buffer, fake_gpt_entries, 512u);
    else return false;
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
    memset(fake_gpt_entries, 0, 512u);
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
    put_le32(fake_gpt_header + 80u, 4u);
    put_le32(fake_gpt_header + 84u, 128u);
    put_le64(fake_gpt_entries + 32u, 2048u);
    put_le64(fake_gpt_entries + 40u, 10000u);
    put_le32(fake_gpt_header + 88u, test_crc32(fake_gpt_entries, 512u));
    put_le32(fake_gpt_header + 16u, test_crc32(fake_gpt_header, 92u));
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
    installer_storage_inventory_t inventory;
    read_calls = 0;
    write_calls = 0;

    prepare_device();
    fake_device_present = false;
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_NO_DEVICE,
           "no ATA device is reported distinctly");

    prepare_device();
    fake_device.io_supported = false;
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.device_detected &&
           inventory.status == INSTALLER_STORAGE_DEVICE_UNREADABLE,
           "detected but unsupported device is not read");

    prepare_device();
    fake_read_success = false;
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_SECTOR_READ_ERROR,
           "sector-zero read failures are reported");

    prepare_device();
    prepare_fat32_boot_sector();
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.sector_zero_read &&
           inventory.status == INSTALLER_STORAGE_FAT32_AT_LBA0,
           "whole-volume FAT32 boot sector is not misclassified as MBR");

    prepare_device();
    prepare_mbr();
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_VALID_MBR &&
           inventory.partition_table.partition_count == 1u,
           "valid MBR is connected to the startup inventory");

    prepare_device();
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    fake_device.addressable_sectors = 1u;
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_GPT_METADATA_UNREADABLE,
           "protective GPT marker is distinguished when LBA1 is inaccessible");


    prepare_device();
    prepare_valid_gpt();
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_VALID_GPT &&
           inventory.gpt_table.valid &&
           inventory.gpt_table.partition_count == 1u,
           "protective MBR leads to CRC-validated GPT inventory");

    prepare_device();
    prepare_valid_gpt();
    fake_gpt_entries[200] ^= 1u;
    memset(fake_sector_zero, 0, sizeof(fake_sector_zero));
    fake_sector_zero[510] = 0x55u;
    fake_sector_zero[511] = 0xAAu;
    set_mbr_entry(fake_sector_zero, 0, 0xEEu, 1u, 999999u);
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_INVALID_PARTITION_METADATA,
           "GPT array CRC corruption is rejected during real inventory scan");

    prepare_device();
    prepare_mbr();
    set_mbr_entry(fake_sector_zero, 1, 0x07u, 10000u, 500000u);
    installer_storage_init();
    expect(installer_storage_get_inventory(&inventory) &&
           inventory.status == INSTALLER_STORAGE_INVALID_PARTITION_METADATA,
           "malformed overlapping primary partitions are rejected");

    expect(write_calls == 0u,
           "storage discovery never invokes the disk write API");
    expect(read_calls == 11u,
           "only supported devices reach a single sector-zero read");
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
