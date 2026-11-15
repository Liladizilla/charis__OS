/* ata_identify.c - bounded, host-testable ATA IDENTIFY decoding. */
#include <kernel/ata.h>

static void clear_bytes(void* destination, u32 length) {
    u8* bytes = (u8*)destination;
    for (u32 i = 0; i < length; i++) bytes[i] = 0;
}

static void decode_ata_string(char* output, u32 output_size,
                               const u16* words, u32 word_count) {
    if (!output || !output_size || !words) return;

    u32 capacity = output_size - 1u;
    u32 copied = word_count * 2u;
    if (copied > capacity) copied = capacity;

    for (u32 i = 0; i < copied; i++) {
        u16 word = words[i / 2u];
        u8 value = (i & 1u) ? (u8)(word & 0xFFu) : (u8)(word >> 8);

        /* IDENTIFY strings are byte-swapped within words and should be
         * printable ASCII. Replace malformed bytes instead of leaking them
         * into the text UI or serial diagnostics. */
        output[i] = (value >= 0x20u && value <= 0x7Eu)
                  ? (char)value : ' ';
    }
    output[copied] = '\0';

    u32 first = 0;
    while (first < copied && output[first] == ' ') first++;
    if (first) {
        u32 remaining = copied - first;
        for (u32 i = 0; i < remaining; i++)
            output[i] = output[first + i];
        copied = remaining;
        output[copied] = '\0';
    }

    while (copied && output[copied - 1u] == ' ')
        output[--copied] = '\0';
}

static u64 identify_lba48_count(const u16 identify[256]) {
    return ((u64)identify[103] << 48) |
           ((u64)identify[102] << 32) |
           ((u64)identify[101] << 16) |
           (u64)identify[100];
}

bool ata_identify_decode(const u16 identify[256], ata_device_info_t* output) {
    if (!identify || !output) return false;

    clear_bytes(output, (u32)sizeof(*output));

    /* Reject empty, all-ones and packet-device IDENTIFY data. This driver
     * implements ATA fixed disks only; ATAPI requires IDENTIFY PACKET DEVICE. */
    if (identify[0] == 0u || identify[0] == 0xFFFFu ||
        (identify[0] & 0x8000u))
        return false;

    output->detected = true;
    decode_ata_string(output->serial, sizeof(output->serial),
                      &identify[10], 10u);
    decode_ata_string(output->firmware, sizeof(output->firmware),
                      &identify[23], 4u);
    decode_ata_string(output->model, sizeof(output->model),
                      &identify[27], 20u);

    output->logical_sector_bytes = ATA_SECTOR_BYTES;

    /* Word 106 is meaningful only when bits 15:14 are 01. Bit 12 says the
     * logical sector is longer than 256 words; words 117-118 then carry its
     * length in 16-bit words. This driver supports exactly 512-byte sectors. */
    if ((identify[106] & 0xC000u) == 0x4000u &&
        (identify[106] & 0x1000u)) {
        u32 words_per_sector = (u32)identify[117] |
                               ((u32)identify[118] << 16);
        if (words_per_sector < 256u || words_per_sector > 0x7FFFFFFFu)
            return false;
        output->logical_sector_bytes = words_per_sector * 2u;
    }

    output->lba_supported = (identify[49] & (1u << 9)) != 0;
    if (!output->lba_supported) return true;

    u64 lba28_count = (u64)identify[60] |
                      ((u64)identify[61] << 16);
    if (lba28_count > ATA_LBA28_SECTOR_LIMIT)
        return false;

    bool command_words_valid = (identify[83] & 0xC000u) == 0x4000u;
    bool lba48_supported = command_words_valid &&
        (identify[83] & (1u << 10)) != 0 &&
        (identify[86] & (1u << 10)) != 0;

    if (lba48_supported) {
        u64 lba48_count = identify_lba48_count(identify);
        /* ATA LBA48 uses 48 address bits. High bits beyond that are reserved. */
        if (identify[103] != 0u) return false;
        if (lba48_count) {
            output->capacity_sectors = lba48_count;
            output->lba48_supported = true;
        }
    }

    if (!output->capacity_sectors)
        output->capacity_sectors = lba28_count;

    output->io_supported =
        output->capacity_sectors != 0u &&
        lba28_count != 0u &&
        output->logical_sector_bytes == ATA_SECTOR_BYTES;

    /* The current PIO commands encode only LBA[27:0]. Never make an LBA48
     * device appear fully accessible when the I/O path cannot address it.
     * Keep metadata capacity separate from usable I/O capacity: unsupported
     * sector sizes and other non-I/O devices must expose zero addressable
     * sectors, not a misleading count that callers might accidentally use. */
    if (output->io_supported) {
        output->addressable_sectors = lba28_count;
        if (output->addressable_sectors > output->capacity_sectors)
            output->addressable_sectors = output->capacity_sectors;
        if (output->addressable_sectors > ATA_LBA28_SECTOR_LIMIT)
            output->addressable_sectors = ATA_LBA28_SECTOR_LIMIT;
    } else {
        output->addressable_sectors = 0u;
    }

    return true;
}


bool ata_device_lba28_is_addressable(const ata_device_info_t* device, u32 lba) {
    return device && device->detected && device->io_supported &&
           (u64)lba < device->addressable_sectors &&
           (u64)lba < ATA_LBA28_SECTOR_LIMIT;
}
