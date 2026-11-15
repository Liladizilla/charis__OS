/* installer_storage.h - read-only installer device inventory. */
#ifndef CHARISOS_INSTALLER_STORAGE_H
#define CHARISOS_INSTALLER_STORAGE_H

#include <kernel/partition.h>

typedef enum {
    INSTALLER_STORAGE_NOT_SCANNED = 0,
    INSTALLER_STORAGE_NO_DEVICE,
    INSTALLER_STORAGE_DEVICE_UNREADABLE,
    INSTALLER_STORAGE_SECTOR_READ_ERROR,
    INSTALLER_STORAGE_FAT32_AT_LBA0,
    INSTALLER_STORAGE_NO_PARTITION_SIGNATURE,
    INSTALLER_STORAGE_VALID_MBR,
    INSTALLER_STORAGE_VALID_GPT,
    INSTALLER_STORAGE_GPT_METADATA_UNREADABLE,
    INSTALLER_STORAGE_GPT_BACKUP_UNREADABLE,
    INSTALLER_STORAGE_INVALID_GPT_BACKUP,
    INSTALLER_STORAGE_EBR_METADATA_UNREADABLE,
    INSTALLER_STORAGE_INVALID_EBR_CHAIN,
    INSTALLER_STORAGE_EBR_CHAIN_LIMIT,
    INSTALLER_STORAGE_INVALID_PARTITION_METADATA
} installer_storage_status_t;

typedef struct {
    bool device_detected;
    bool sector_zero_read;
    installer_storage_status_t status;
    ata_device_info_t device;
    partition_table_t partition_table;
    gpt_header_info_t gpt_header;
    gpt_partition_table_t gpt_table;
} installer_storage_inventory_t;

/* Scans the primary-master device once using read-only sector I/O. */
void installer_storage_init(void);
const installer_storage_inventory_t* installer_storage_get(void);
const char* installer_storage_status_label(installer_storage_status_t status);

#endif
