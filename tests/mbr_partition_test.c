#include <kernel/partition.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(bool condition, const char* name) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", name);
    failures++;
}

static void put_le32(u8* bytes, u32 value) {
    bytes[0] = (u8)value;
    bytes[1] = (u8)(value >> 8);
    bytes[2] = (u8)(value >> 16);
    bytes[3] = (u8)(value >> 24);
}

static void set_entry(u8 sector[512], u32 index, u8 boot, u8 type,
                      u32 first_lba, u32 sector_count) {
    u8* entry = &sector[446u + index * 16u];
    entry[0] = boot;
    entry[4] = type;
    put_le32(&entry[8], first_lba);
    put_le32(&entry[12], sector_count);
}

static void prepare_mbr(u8 sector[512]) {
    memset(sector, 0, 512u);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
}

static ata_device_info_t make_device(void) {
    ata_device_info_t device;
    memset(&device, 0, sizeof(device));
    device.detected = true;
    device.io_supported = true;
    device.logical_sector_bytes = 512u;
    device.capacity_sectors = 1000000u;
    device.addressable_sectors = 1000000u;
    return device;
}

int main(void) {
    u8 sector[512];
    partition_table_t table;
    ata_device_info_t device = make_device();

    prepare_mbr(sector);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "valid empty MBR sector parses");
    expect(table.valid && table.scheme == PARTITION_SCHEME_MBR &&
           table.partition_count == 0u && table.fully_enumerated,
           "empty MBR is distinguishable from an absent signature");

    prepare_mbr(sector);
    set_entry(sector, 0, 0x80u, 0x0Cu, 2048u, 100000u);
    set_entry(sector, 1, 0x00u, 0x07u, 102048u, 200000u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "two adjacent primary partitions parse");
    expect(table.partition_count == 2u &&
           table.partitions[0].bootable &&
           table.partitions[0].accessible &&
           table.partitions[1].type == 0x07u,
           "primary partition metadata is preserved");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Fu, 2048u, 600000u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "extended partition container is parsed");
    expect(table.has_extended && !table.fully_enumerated &&
           table.partitions[0].extended,
           "extended chain is explicitly marked incomplete");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0xEEu, 1u, 999999u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "protective GPT MBR parses");
    expect(table.scheme == PARTITION_SCHEME_GPT_PROTECTIVE &&
           !table.fully_enumerated,
           "protective MBR does not imply GPT header/array parsing");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0xEEu, 1u, 999999u);
    set_entry(sector, 1, 0, 0x07u, 2048u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table) &&
           table.scheme == PARTITION_SCHEME_INVALID,
           "hybrid protective MBR is rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Cu, 2048u, 100000u);
    set_entry(sector, 1, 0, 0x07u, 50000u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "overlapping primary partitions are rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0x7Fu, 0x0Cu, 2048u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "invalid boot flag is rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Cu, 2048u, 0u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "non-empty partition with zero length is rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Cu, 0u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "partition starting at LBA zero is rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Cu, 900000u, 200000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "partition extending beyond disk capacity is rejected");

    prepare_mbr(sector);
    set_entry(sector, 0, 0, 0x0Cu, 200000u, 10000u);
    device.addressable_sectors = 100000u;
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "valid partition beyond current LBA28 access window is inventoried");
    expect(table.partitions[0].present && !table.partitions[0].accessible,
           "partition geometry and current readability are separate");
    device = make_device();

    memset(sector, 0, sizeof(sector));
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "missing MBR signature is a non-error absence result");
    expect(table.scheme == PARTITION_SCHEME_NONE && !table.valid,
           "missing signature is never called an empty valid MBR");

    prepare_mbr(sector);
    device.logical_sector_bytes = 4096u;
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "non-512-byte device is not parsed as an MBR sector");
    device = make_device();

    expect(!partition_parse_mbr_sector(NULL, &device, &table),
           "null sector is rejected");
    expect(!partition_parse_mbr_sector(sector, NULL, &table),
           "null device metadata is rejected");
    expect(!partition_parse_mbr_sector(sector, &device, NULL),
           "null output is rejected");

    if (failures) {
        fprintf(stderr, "MBR parser: %d failure(s)\n", failures);
        return 1;
    }
    puts("MBR parser: all checks passed");
    return 0;
}
