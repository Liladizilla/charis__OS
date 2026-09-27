/* disk.h - ATA PIO disk driver */
#pragma once
#include <kernel/types.h>

#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE_HEAD  0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

#define ATA_SECONDARY_DATA      0x170

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_CACHE_FLUSH     0xE7

#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DRQ              0x08
#define ATA_SR_ERR              0x01

typedef struct {
    u16 base;
    u16 ctrl;
    bool present;
    u64 sectors;
    char model[41];
} ata_device_t;

void disk_init(void);
bool disk_read_sectors(u64 lba, u32 count, void* buffer);
bool disk_write_sectors(u64 lba, u32 count, const void* buffer);
bool disk_identify(ata_device_t* dev);

