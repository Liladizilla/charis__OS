#include <kernel/ata.h>
#include <kernel/vga.h>
#include <kernel/io.h>

#define ATA_SECTOR_SIZE 512

// ATA registers (primary channel)
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

// Status bits
#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_RDY  0x40
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

// Commands
#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_IDENTIFY 0xEC

void ata_wait_bsy(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(inb(ATA_STATUS) & ATA_STATUS_BSY)) return;
    }
}

void ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        if (inb(ATA_STATUS) & ATA_STATUS_DRQ) return;
    }
}

bool ata_read_sector(u32 lba, void* buffer) {
    ata_wait_bsy();

    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_READ);

    ata_wait_bsy();
    if (inb(ATA_STATUS) & ATA_STATUS_ERR) return false;

    ata_wait_drq();

    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        ((u16*)buffer)[i] = inw(ATA_DATA);
    }

    return true;
}

bool ata_write_sector(u32 lba, const void* buffer) {
    ata_wait_bsy();

    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    ata_wait_bsy();
    if (inb(ATA_STATUS) & ATA_STATUS_ERR) return false;

    ata_wait_drq();

    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        outw(ATA_DATA, ((u16*)buffer)[i]);
    }

    // Wait for write to complete
    ata_wait_bsy();

    return true;
}

void ata_init(void) {
    // Identify drive
    ata_wait_bsy();
    outb(ATA_DRIVE, 0xA0); // Master drive
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    ata_wait_bsy();
    if (inb(ATA_STATUS) == 0) {
        vga_puts("ATA: No drive detected\n");
        return;
    }

    // Skip reading identify data for now
    vga_puts("ATA initialized\n");
}