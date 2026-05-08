#ifndef ATA_H
#define ATA_H

#include <kernel/types.h>

void ata_init(void);
bool ata_read_sector(u32 lba, void* buffer);
bool ata_write_sector(u32 lba, const void* buffer);

#endif