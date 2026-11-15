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

static void put_le64(u8* bytes, u64 value) {
    put_le32(bytes, (u32)value);
    put_le32(bytes + 4u, (u32)(value >> 32));
}

static u32 crc32(const u8* bytes, u32 length) {
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8u; bit++)
            crc = (crc >> 1) ^ ((u32)-(s32)(crc & 1u) & 0xEDB88320u);
    }
    return crc ^ 0xFFFFFFFFu;
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

static void build_gpt(u8 header[512], u8 entries[GPT_MAX_ARRAY_BYTES],
                      u32 entry_count, u32 entry_size,
                      u64 first, u64 last) {
    memset(header, 0, 512u);
    memset(entries, 0, GPT_MAX_ARRAY_BYTES);
    memcpy(header, "EFI PART", 8u);
    put_le32(header + 8u, 0x00010000u);
    put_le32(header + 12u, 92u);
    put_le64(header + 24u, 1u);
    put_le64(header + 32u, 999999u);
    put_le64(header + 40u, 34u);
    put_le64(header + 48u, 999966u);
    for (u32 i = 0; i < 16u; i++) {
        header[56u + i] = (u8)(i + 1u);
    }
    put_le64(header + 72u, 2u);
    put_le32(header + 80u, entry_count);
    put_le32(header + 84u, entry_size);

    if (entry_count && entry_size >= 128u) {
        u8* entry = entries;
        for (u32 i = 0; i < 16u; i++) {
            entry[i] = (u8)(0xA0u + i);
            entry[16u + i] = (u8)(0x10u + i);
        }
        put_le64(entry + 32u, first);
        put_le64(entry + 40u, last);
    }

    u32 bytes = entry_count * entry_size;
    if (bytes > GPT_MAX_ARRAY_BYTES) bytes = GPT_MAX_ARRAY_BYTES;
    put_le32(header + 88u, crc32(entries, bytes));
    put_le32(header + 16u, crc32(header, 92u));
}

static void test_mbr(void) {
    u8 sector[512];
    partition_table_t table;
    ata_device_info_t device = make_device();

    memset(sector, 0, sizeof(sector));
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "valid empty MBR sector parses");
    expect(table.valid && table.scheme == PARTITION_SCHEME_MBR &&
           table.partition_count == 0u && table.fully_enumerated,
           "empty MBR is distinguishable from an absent signature");

    memset(sector, 0, sizeof(sector));
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    sector[446u] = 0x80u;
    sector[450u] = 0x0Cu;
    put_le32(sector + 454u, 2048u);
    put_le32(sector + 458u, 100000u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "valid primary MBR entry parses");
    expect(table.partition_count == 1u && table.partitions[0].bootable &&
           table.partitions[0].accessible,
           "MBR bootability and accessibility preserved");

    memset(sector, 0, sizeof(sector));
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    sector[450u] = 0xEEu;
    put_le32(sector + 454u, 1u);
    put_le32(sector + 458u, 999999u);
    expect(partition_parse_mbr_sector(sector, &device, &table) &&
           table.scheme == PARTITION_SCHEME_GPT_PROTECTIVE &&
           !table.fully_enumerated,
           "protective MBR identified without claiming GPT parsing");
}

static void test_gpt(void) {
    u8 header_sector[512];
    u8 entries[GPT_MAX_ARRAY_BYTES];
    gpt_header_info_t header;
    gpt_partition_table_t table;
    ata_device_info_t device = make_device();

    build_gpt(header_sector, entries, 4u, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "valid GPT primary header passes signature, bounds and CRC");
    expect(header.entry_array_lba == 2u && header.entry_array_bytes == 512u &&
           header.entry_array_sectors == 1u,
           "GPT entry array read plan is exact");
    expect(partition_parse_gpt_entries(&header, entries, 512u, &device, &table),
           "valid GPT entry array passes CRC and range checks");
    expect(table.valid && table.fully_enumerated &&
           table.partition_count == 1u &&
           table.partitions[0].first_lba == 2048u &&
           table.partitions[0].last_lba == 10000u,
           "GPT partition geometry is preserved");

    entries[200] ^= 1u;
    expect(!partition_parse_gpt_entries(&header, entries, 512u, &device, &table),
           "GPT partition-array CRC mismatch is rejected");
    entries[200] ^= 1u;

    header_sector[92] ^= 1u;
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "bytes outside GPT header_size do not affect header CRC");
    header_sector[16] ^= 1u;
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "GPT header CRC mismatch is rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS + 1u, 128u, 2048u, 10000u);
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "entry count above the hard cap is rejected");

    build_gpt(header_sector, entries, 4u, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "valid header reparses before partition edge cases");
    put_le64(entries + 40u, 1000001u);
    put_le32(header_sector + 88u, crc32(entries, 512u));
    put_le32(header_sector + 16u, 0u);
    put_le32(header_sector + 16u, crc32(header_sector, 92u));
    expect(!partition_parse_gpt_entries(&header, entries, 512u, &device, &table),
           "partition beyond disk capacity is rejected");

    build_gpt(header_sector, entries, 4u, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "GPT header passes before limited-access test");
    device.addressable_sectors = 4096u;
    expect(partition_parse_gpt_entries(&header, entries, 512u, &device, &table) &&
           !table.partitions[0].accessible,
           "partition geometry is separated from current I/O access");

    expect(!partition_parse_gpt_header(NULL, &device, &header),
           "null GPT header is rejected");
    expect(!partition_parse_gpt_entries(&header, entries, 512u, NULL, &table),
           "null GPT device is rejected");
}

int main(void) {
    test_mbr();
    test_gpt();
    if (failures) {
        fprintf(stderr, "Partition parser: %d failure(s)\n", failures);
        return 1;
    }
    puts("Partition parser: all checks passed");
    return 0;
}
