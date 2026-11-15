#ifndef ATA_H
#define ATA_H

#include <kernel/types.h>

#define ATA_MODEL_LENGTH 40u
#define ATA_SERIAL_LENGTH 20u
#define ATA_FIRMWARE_LENGTH 8u
#define ATA_SECTOR_BYTES 512u
#define ATA_LBA28_SECTOR_LIMIT 0x10000000ULL

typedef struct {
    bool detected;
    bool lba_supported;
    bool lba48_supported;
    bool io_supported;
    u32 logical_sector_bytes;
    u64 capacity_sectors;
    /* Number of sectors safely addressable by the current LBA28 PIO path. */
    u64 addressable_sectors;
    char model[ATA_MODEL_LENGTH + 1u];
    char serial[ATA_SERIAL_LENGTH + 1u];
    char firmware[ATA_FIRMWARE_LENGTH + 1u];
} ata_device_info_t;

/* Decode the 256-word ATA IDENTIFY DEVICE payload without hardware I/O. */
bool ata_identify_decode(const u16 identify[256], ata_device_info_t* output);
bool ata_device_lba28_is_addressable(const ata_device_info_t* device, u32 lba);

void ata_init(void);
bool ata_is_present(void);
/* Returns false when no ATA device was identified; unsupported devices still
 * return metadata with io_supported=false for inventory/diagnostics. */
bool ata_get_device_info(ata_device_info_t* output);
bool ata_read_sector(u32 lba, void* buffer);
bool ata_write_sector(u32 lba, const void* buffer);

#endif
