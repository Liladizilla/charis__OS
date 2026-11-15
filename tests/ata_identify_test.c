#include <kernel/ata.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(bool condition, const char* name) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", name);
    failures++;
}

static void set_string(u16 identify[256], u32 first_word,
                       u32 word_count, const char* value) {
    u32 length = word_count * 2u;
    u32 value_length = value ? (u32)strlen(value) : 0u;
    for (u32 i = 0; i < length; i += 2u) {
        u8 high = i < value_length ? (u8)value[i] : (u8)' ';
        u8 low = i + 1u < value_length ? (u8)value[i + 1u] : (u8)' ';
        identify[first_word + i / 2u] = (u16)(((u16)high << 8) | low);
    }
}

static void make_lba28(u16 identify[256], u32 sectors) {
    memset(identify, 0, 256u * sizeof(u16));
    identify[0] = 0x0040u; /* ATA fixed device */
    identify[49] = (1u << 9);
    identify[60] = (u16)(sectors & 0xFFFFu);
    identify[61] = (u16)(sectors >> 16);
    set_string(identify, 10, 10, "SERIAL-CHARIS-0001");
    set_string(identify, 23, 4, "FW1.25");
    set_string(identify, 27, 20, "CHARIS TEST DISK");
}

int main(void) {
    u16 identify[256];
    ata_device_info_t info;

    make_lba28(identify, 40000000u);
    expect(ata_identify_decode(identify, &info),
           "valid LBA28 IDENTIFY payload is accepted");
    expect(info.detected && info.lba_supported && info.io_supported,
           "LBA28 ATA disk is identified as readable");
    expect(info.capacity_sectors == 40000000u &&
           info.addressable_sectors == 40000000u,
           "LBA28 sector capacity is decoded exactly");
    expect(info.logical_sector_bytes == 512u,
           "default logical sector size is 512 bytes");
    expect(strcmp(info.serial, "SERIAL-CHARIS-0001") == 0,
           "serial bytes are swapped and trimmed");
    expect(strcmp(info.firmware, "FW1.25") == 0,
           "firmware bytes are decoded");
    expect(strcmp(info.model, "CHARIS TEST DISK") == 0,
           "model bytes are decoded");

    /* LBA48 device: report real capacity, but clamp current driver access to
     * the 28-bit range until READ/WRITE SECTORS EXT is implemented. */
    make_lba28(identify, 0x0FFFFFFFu);
    identify[83] = 0x4400u; /* Valid command-set word + LBA48 support. */
    identify[86] = 0x4400u; /* LBA48 enabled. */
    identify[100] = (u16)(2000000000ULL & 0xFFFFu);
    identify[101] = (u16)((2000000000ULL >> 16) & 0xFFFFu);
    identify[102] = 0;
    identify[103] = 0;
    expect(ata_identify_decode(identify, &info),
           "valid LBA48 IDENTIFY payload is accepted");
    expect(info.lba48_supported && info.capacity_sectors == 2000000000ULL,
           "LBA48 capacity is reported without truncation");
    expect(info.addressable_sectors == 0x0FFFFFFFu,
           "current LBA28 path is explicitly clamped");
    expect(info.io_supported,
           "LBA48 device remains usable inside the LBA28 address window");
    expect(ata_device_lba28_is_addressable(&info, 0u),
           "first LBA is addressable");
    expect(ata_device_lba28_is_addressable(&info,
               (u32)(info.addressable_sectors - 1u)),
           "last in-capacity LBA is addressable");
    expect(!ata_device_lba28_is_addressable(&info,
                (u32)info.addressable_sectors),
           "first LBA beyond reported capacity is rejected");
    expect(!ata_device_lba28_is_addressable(NULL, 0u),
           "null device record is rejected");


    /* Logical sectors larger than 512 bytes are detected but not exposed to
     * the fixed 512-byte sector I/O functions. */
    make_lba28(identify, 1000000u);
    identify[106] = 0x5000u; /* Valid + logical sector > 256 words. */
    identify[117] = 2048u;   /* 4096-byte logical sector. */
    expect(ata_identify_decode(identify, &info),
           "valid large-sector ATA device is still identified");
    expect(info.detected && !info.io_supported &&
           info.logical_sector_bytes == 4096u,
           "unsupported 4K logical sectors are not presented as readable");
    expect(info.addressable_sectors == 0u,
           "unsupported sector sizes expose no addressable PIO sectors");
    expect(!ata_device_lba28_is_addressable(&info, 0u),
           "unsupported sector sizes reject every LBA28 read");

    make_lba28(identify, 1000u);
    identify[49] = 0;
    expect(ata_identify_decode(identify, &info),
           "ATA device without LBA is still inventoried");
    expect(info.detected && !info.lba_supported && !info.io_supported &&
           info.addressable_sectors == 0u,
           "non-LBA device is not exposed through the LBA PIO path");

    make_lba28(identify, 0u);
    expect(ata_identify_decode(identify, &info),
           "device with zero reported sectors remains identifiable");
    expect(info.detected && !info.io_supported,
           "zero-capacity device is not considered usable");

    make_lba28(identify, 1000u);
    identify[0] = 0x8000u;
    expect(!ata_identify_decode(identify, &info),
           "ATAPI packet device signature is rejected");
    expect(!info.detected,
           "rejected packet signature leaves output cleared");

    make_lba28(identify, 1000u);
    identify[103] = 1u;
    identify[83] = 0x4400u;
    identify[86] = 0x4400u;
    expect(!ata_identify_decode(identify, &info),
           "reserved LBA48 high bits are rejected");

    expect(!ata_identify_decode(NULL, &info),
           "null IDENTIFY input is rejected");
    expect(!ata_identify_decode(identify, NULL),
           "null output is rejected");

    if (failures) {
        fprintf(stderr, "ATA IDENTIFY: %d failure(s)\n", failures);
        return 1;
    }
    puts("ATA IDENTIFY: all checks passed");
    return 0;
}
