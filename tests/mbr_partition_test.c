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

static u32 read_le32_test(const u8* bytes) {
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static u64 read_le64_test(const u8* bytes) {
    return (u64)bytes[0] |
           ((u64)bytes[1] << 8) |
           ((u64)bytes[2] << 16) |
           ((u64)bytes[3] << 24) |
           ((u64)bytes[4] << 32) |
           ((u64)bytes[5] << 40) |
           ((u64)bytes[6] << 48) |
           ((u64)bytes[7] << 56);
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

static void prepare_mbr(u8 sector[512]) {
    memset(sector, 0, 512u);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
}

static void set_mbr_entry(u8 sector[512], u32 index, u8 boot, u8 type,
                          u32 first, u32 count) {
    u8* entry = &sector[446u + index * 16u];
    entry[0] = boot;
    entry[4] = type;
    put_le32(&entry[8], first);
    put_le32(&entry[12], count);
}

static void test_mbr(void) {
    u8 sector[512];
    partition_table_t table;
    ata_device_info_t device = make_device();

    prepare_mbr(sector);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "valid empty MBR sector parses");
    expect(table.valid && table.scheme == PARTITION_SCHEME_MBR &&
           table.partition_count == 0u && table.fully_enumerated,
           "empty MBR is distinguishable from a missing signature");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0x80u, 0x0Cu, 2048u, 100000u);
    set_mbr_entry(sector, 1, 0x00u, 0x07u, 102048u, 200000u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "two adjacent primary partitions parse");
    expect(table.partition_count == 2u &&
           table.partitions[0].bootable &&
           table.partitions[0].accessible &&
           table.partitions[1].type == 0x07u,
           "MBR boot flags, types and ranges are preserved");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Fu, 2048u, 600000u);
    expect(partition_parse_mbr_sector(sector, &device, &table),
           "extended partition container parses");
    expect(table.has_extended && !table.fully_enumerated &&
           table.partitions[0].extended,
           "extended chain is not marked complete before EBR traversal");
    expect(table.extended_base_lba == 2048u &&
           table.extended_end_lba == 602048u,
           "extended container bounds are preserved for the EBR scanner");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0xEEu, 1u, 999999u);
    expect(partition_parse_mbr_sector(sector, &device, &table) &&
           table.scheme == PARTITION_SCHEME_GPT_PROTECTIVE &&
           !table.fully_enumerated,
           "protective GPT MBR is recognized without parsing GPT implicitly");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0xEEu, 1u, 999999u);
    set_mbr_entry(sector, 1, 0, 0x07u, 2048u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table) &&
           table.scheme == PARTITION_SCHEME_INVALID,
           "hybrid protective MBR is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Cu, 2048u, 100000u);
    set_mbr_entry(sector, 1, 0, 0x07u, 50000u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "overlapping primary partitions are rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0x7Fu, 0x0Cu, 2048u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "invalid boot indicator is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Cu, 2048u, 0u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "non-empty partition with zero length is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Cu, 0u, 100000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "partition starting at the MBR itself is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Cu, 900000u, 200000u);
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "partition extending beyond device capacity is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Cu, 200000u, 10000u);
    device.addressable_sectors = 100000u;
    expect(partition_parse_mbr_sector(sector, &device, &table) &&
           table.partitions[0].present && !table.partitions[0].accessible,
           "partition presence is distinct from current LBA28 accessibility");
    device = make_device();

    memset(sector, 0, sizeof(sector));
    expect(partition_parse_mbr_sector(sector, &device, &table) &&
           table.scheme == PARTITION_SCHEME_NONE && !table.valid,
           "missing MBR signature is not treated as a valid empty table");

    prepare_mbr(sector);
    device.logical_sector_bytes = 4096u;
    expect(!partition_parse_mbr_sector(sector, &device, &table),
           "non-512-byte logical sector devices are not parsed as MBR");
    device = make_device();

    expect(!partition_parse_mbr_sector(NULL, &device, &table),
           "null MBR sector is rejected");
    expect(!partition_parse_mbr_sector(sector, NULL, &table),
           "null device metadata is rejected");
    expect(!partition_parse_mbr_sector(sector, &device, NULL),
           "null MBR output is rejected");
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

    for (u32 i = 0; i < 16u; i++)
        header[56u + i] = (u8)(i + 1u);

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

    u64 bytes_wide = (u64)entry_count * entry_size;
    u32 bytes = bytes_wide > GPT_MAX_ARRAY_BYTES
              ? GPT_MAX_ARRAY_BYTES : (u32)bytes_wide;
    put_le32(header + 88u, crc32(entries, bytes));
    put_le32(header + 16u, crc32(header, 92u));
}

static void refresh_gpt_crcs(u8 header[512], u8* entries, u32 array_bytes) {
    put_le32(header + 88u, crc32(entries, array_bytes));
    put_le32(header + 16u, 0u);
    put_le32(header + 16u, crc32(header, 92u));
}

static void build_backup_gpt(u8 header[512],
                               const u8 primary_header[512],
                               u64 backup_array_lba,
                               const u8* entries,
                               u32 entry_bytes) {
    memset(header, 0, 512u);
    memcpy(header, "EFI PART", 8u);
    put_le32(header + 8u, 0x00010000u);
    put_le32(header + 12u, 92u);
    put_le64(header + 24u, 999999u);
    put_le64(header + 32u, 1u);
    put_le64(header + 40u, read_le64_test(primary_header + 40u));
    put_le64(header + 48u, read_le64_test(primary_header + 48u));
    memcpy(header + 56u, primary_header + 56u, 16u);
    put_le64(header + 72u, backup_array_lba);
    put_le32(header + 80u, read_le32_test(primary_header + 80u));
    put_le32(header + 84u, read_le32_test(primary_header + 84u));
    put_le32(header + 88u, crc32(entries, entry_bytes));
    put_le32(header + 16u, crc32(header, 92u));
}

static void test_ebr(void) {
    u8 sector[512];
    ata_device_info_t device = make_device();
    ebr_sector_info_t decoded;
    const u64 base = 10000u;
    const u64 end = 510000u;

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 63u, 50000u);
    set_mbr_entry(sector, 1, 0, 0x0Fu, 100000u, 400000u);
    expect(partition_parse_ebr_sector(sector, base, base, end,
                                      &device, &decoded),
           "valid EBR record parses");
    expect(decoded.logical_partition.present &&
           decoded.logical_partition.first_lba == 10063u &&
           decoded.logical_partition.sector_count == 50000u &&
           decoded.logical_partition.accessible,
           "logical partition start is relative to its own EBR");
    expect(decoded.has_next_ebr && decoded.next_ebr_lba == 110000u,
           "next EBR pointer is relative to the extended partition base");

    expect(decoded.next_ebr_end_lba == 510000u,
           "the next EBR link's declared extent is preserved");

    /* The next EBR and its data must fit inside the previous link's range. */
    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 63u, 1000u);
    set_mbr_entry(sector, 1, 0, 0x0Fu, 100000u, 5000u);
    expect(partition_parse_ebr_sector(sector, base, base, end,
                                      &device, &decoded),
           "bounded parent link decodes");
    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x07u, 1000u, 5000u);
    expect(!partition_parse_ebr_sector(sector, decoded.next_ebr_lba,
                                       base, decoded.next_ebr_end_lba,
                                       &device, &decoded),
           "logical data beyond the parent link extent is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x07u, 63u, 1000u);
    expect(partition_parse_ebr_sector(sector, 110000u, base, end,
                                      &device, &decoded) &&
           !decoded.has_next_ebr,
           "terminal EBR has no next link");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x0Fu, 63u, 1000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "logical entry cannot itself be an extended container");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 400000u, 200000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "logical partition extending past the container is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 63u, 1000u);
    set_mbr_entry(sector, 1, 0, 0x0Fu, 500000u, 10000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "next EBR link beyond the extended container is rejected");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 63u, 1000u);
    set_mbr_entry(sector, 1, 0, 0x0Fu, 63u, 1000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "next EBR cannot point inside the current logical partition");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 2000u, 1000u);
    set_mbr_entry(sector, 1, 0, 0x0Fu, 1000u, 3000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "the full next-EBR link extent cannot overlap logical data");

    prepare_mbr(sector);
    set_mbr_entry(sector, 0, 0, 0x83u, 63u, 1000u);
    set_mbr_entry(sector, 2, 0, 0x83u, 2000u, 1000u);
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "reserved EBR entries three and four must remain empty");

    prepare_mbr(sector);
    sector[510] = 0u;
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, &decoded),
           "EBR signature is mandatory");

    expect(!partition_parse_ebr_sector(NULL, base, base, end,
                                       &device, &decoded),
           "null EBR sector is rejected");
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       NULL, &decoded),
           "null EBR device is rejected");
    expect(!partition_parse_ebr_sector(sector, base, base, end,
                                       &device, NULL),
           "null EBR output is rejected");
}

static void test_gpt(void) {
    u8 header_sector[512];
    u8 entries[GPT_MAX_ARRAY_BYTES];
    gpt_header_info_t header;
    gpt_partition_table_t table;
    ata_device_info_t device = make_device();

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "valid GPT primary header passes signature, bounds and CRC");
    expect(header.entry_array_lba == 2u &&
           header.entry_array_bytes == GPT_MIN_RESERVED_BYTES &&
           header.entry_array_sectors == 32u,
           "GPT entry-array read plan is exact");
    expect(partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "valid GPT entry array passes CRC and range checks");
    gpt_header_info_t malformed_entry_header = header;
    malformed_entry_header.entry_size = 136u;
    malformed_entry_header.entry_array_bytes = 136u * GPT_MAX_PARTITIONS;
    expect(!partition_parse_gpt_entries(&malformed_entry_header, entries,
                GPT_MAX_ARRAY_BYTES, &device, &table),
           "standalone GPT array parser rejects non-UEFI entry sizes");
    expect(table.valid && table.fully_enumerated &&
           table.partition_count == 1u &&
           table.partitions[0].first_lba == 2048u &&
           table.partitions[0].last_lba == 10000u,
           "GPT partition geometry is preserved");

    u8 backup_header_sector[512];
    gpt_header_info_t backup_header;
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);
    expect(partition_parse_gpt_backup_header(backup_header_sector, &device,
                                             &header, &backup_header),
           "reciprocal backup GPT header passes CRC and metadata comparison");
    expect(backup_header.current_lba == 999999u &&
           backup_header.backup_lba == 1u &&
           backup_header.entry_array_lba == 999967u,
           "backup GPT location and reciprocal LBA fields are preserved");

    backup_header_sector[16] ^= 1u;
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT header CRC corruption is rejected");
    backup_header_sector[16] ^= 1u;

    put_le64(backup_header_sector + 32u, 2u);
    put_le32(backup_header_sector + 16u, 0u);
    put_le32(backup_header_sector + 16u, crc32(backup_header_sector, 92u));
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT must point back to primary LBA 1");
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);

    backup_header_sector[56] ^= 1u;
    put_le32(backup_header_sector + 16u, 0u);
    put_le32(backup_header_sector + 16u, crc32(backup_header_sector, 92u));
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT disk GUID must match the primary header");
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);

    put_le32(backup_header_sector + 88u,
             read_le32_test(backup_header_sector + 88u) ^ 1u);
    put_le32(backup_header_sector + 16u, 0u);
    put_le32(backup_header_sector + 16u, crc32(backup_header_sector, 92u));
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT array CRC must match the primary header");
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);

    put_le32(backup_header_sector + 80u, 8u);
    put_le32(backup_header_sector + 16u, 0u);
    put_le32(backup_header_sector + 16u, crc32(backup_header_sector, 92u));
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT entry count must match primary metadata");
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);

    put_le32(backup_header_sector + 84u, 256u);
    put_le32(backup_header_sector + 16u, 0u);
    put_le32(backup_header_sector + 16u, crc32(backup_header_sector, 92u));
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT entry size must match primary metadata");
    build_backup_gpt(backup_header_sector, header_sector, 999967u,
                     entries, header.entry_array_bytes);

    build_backup_gpt(backup_header_sector, header_sector, 999965u,
                     entries, header.entry_array_bytes);
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT entry array cannot overlap the usable region");

    build_backup_gpt(backup_header_sector, header_sector, 999999u,
                     entries, header.entry_array_bytes);
    expect(!partition_parse_gpt_backup_header(backup_header_sector, &device,
                                              &header, &backup_header),
           "backup GPT entry array cannot overlap its header");

    /* The primary header must identify the final LBA as its backup. */
    put_le64(header_sector + 32u, 999998u);
    put_le32(header_sector + 16u, 0u);
    put_le32(header_sector + 16u, crc32(header_sector, 92u));
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "primary GPT alternate LBA must identify the disk's final sector");
    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "valid primary GPT header is restored after alternate-LBA test");

    entries[200] ^= 1u;
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "GPT partition-array CRC mismatch is rejected");
    entries[200] ^= 1u;

    header_sector[511] ^= 1u;
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "nonzero bytes in the reserved GPT header tail are rejected");
    header_sector[511] ^= 1u;
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "zeroed GPT reserved tail restores valid header parsing");
    header_sector[16] ^= 1u;
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "GPT header CRC mismatch is rejected");

    build_gpt(header_sector, entries, 4u, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header) &&
           header.entry_array_bytes == 512u,
           "small GPT payload is valid when its 16 KiB reservation is present");
    put_le64(header_sector + 40u, 33u);
    put_le32(header_sector + 16u, 0u);
    put_le32(header_sector + 16u, crc32(header_sector, 92u));
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "GPT primary reservation smaller than 16 KiB is rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 136u, 2048u, 10000u);
    put_le64(header_sector + 40u, 40u);
    put_le32(header_sector + 16u, 0u);
    put_le32(header_sector + 16u, crc32(header_sector, 92u));
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "GPT entry sizes outside 128 times powers of two are rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS + 1u, 128u, 2048u, 10000u);
    expect(!partition_parse_gpt_header(header_sector, &device, &header),
           "entry count above the hard cap is rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "valid header reparses before partition edge cases");

    put_le64(entries + 40u, 1000001u);
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "header CRC follows a rebuilt partition array checksum");
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "partition beyond disk capacity is rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    u8* second = entries + 128u;
    for (u32 i = 0; i < 16u; i++) {
        second[i] = (u8)(0xB0u + i);
        second[16u + i] = (u8)(0x20u + i);
    }
    put_le64(second + 32u, 9000u);
    put_le64(second + 40u, 12000u);
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "overlap fixture GPT header validates");
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "overlapping GPT partition extents are rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    second = entries + 128u;
    for (u32 i = 0; i < 16u; i++) second[i] = (u8)(0xB0u + i);
    /* Keep the same unique GUID as entry zero to test GUID uniqueness. */
    for (u32 i = 0; i < 16u; i++) second[16u + i] = entries[16u + i];
    put_le64(second + 32u, 15000u);
    put_le64(second + 40u, 16000u);
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "duplicate-GUID fixture header validates");
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "duplicate GPT unique partition GUIDs are rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    memset(entries + 16u, 0, 16u);
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "zero unique-GUID fixture header validates");
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table),
           "used GPT entry with zero unique GUID is rejected");

    build_gpt(header_sector, entries, 64u, 256u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header) &&
           header.entry_array_bytes == GPT_MIN_RESERVED_BYTES &&
           header.entry_array_sectors == 32u,
           "256-byte GPT entries fit the minimum reserved array");
    expect(partition_parse_gpt_entries(&header, entries,
               header.entry_array_bytes, &device, &table),
           "256-byte GPT entries with zero reserved tails are accepted");

    entries[128u] = 1u;
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "active-entry reserved-tail fixture has a valid header and CRC");
    expect(!partition_parse_gpt_entries(&header, entries,
                header.entry_array_bytes, &device, &table),
           "nonzero reserved extension bytes in an active GPT entry are rejected");

    entries[128u] = 0u;
    entries[256u + 128u] = 1u;
    refresh_gpt_crcs(header_sector, entries, GPT_MIN_RESERVED_BYTES);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "unused-entry reserved-tail fixture has a valid header and CRC");
    expect(!partition_parse_gpt_entries(&header, entries,
                header.entry_array_bytes, &device, &table),
           "nonzero reserved extension bytes in an unused GPT slot are rejected");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "base-size GPT entries remain valid after extension-tail tests");

    build_gpt(header_sector, entries, GPT_MAX_PARTITIONS, 128u, 2048u, 10000u);
    expect(partition_parse_gpt_header(header_sector, &device, &header),
           "GPT header passes before limited-access test");
    device.addressable_sectors = 4096u;
    expect(partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, &device, &table) &&
           !table.partitions[0].accessible,
           "partition geometry is separated from current I/O access");
    device = make_device();

    expect(!partition_parse_gpt_header(NULL, &device, &header),
           "null GPT header is rejected");
    expect(!partition_parse_gpt_entries(&header, entries, header.entry_array_bytes, NULL, &table),
           "null GPT device is rejected");
    expect(!partition_parse_gpt_entries(&header, NULL, 512u, &device, &table),
           "null GPT array is rejected");
}

int main(void) {
    test_mbr();
    test_ebr();
    test_gpt();
    if (failures) {
        fprintf(stderr, "Partition parser: %d failure(s)\n", failures);
        return 1;
    }
    puts("Partition parser: all checks passed");
    return 0;
}
