#include <kernel/disk.h>
#include <kernel/vga.h>
#include <kernel/irq.h>
#include <kernel/io.h>

static ata_device_t primary_master = {
    .base = ATA_PRIMARY_DATA,
    .ctrl = 0,
    .present = false,
    .sectors = 0,
    .model = {0}
};

static void disk_wait_ready(void) {
    // Wait for BSY to clear
    for (int i = 0; i < 100000; i++) {
        u8 status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return;
        }
    }
}

static void disk_select_drive(u8 drive) {
    outb(ATA_PRIMARY_DRIVE_HEAD, drive);
    io_delay();
}

bool disk_identify(ata_device_t* dev) {
    if (!dev) return false;
    
    disk_wait_ready();
    disk_select_drive(0xA0); // Master drive
    
    outb(ATA_PRIMARY_SECTOR_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LOW, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HIGH, 0);
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    io_delay();
    
    // Poll for ready
    disk_wait_ready();
    
    u8 status = inb(ATA_PRIMARY_STATUS);
    if (status & ATA_SR_ERR) {
        return false;
    }
    
    // Read identify data
    u16 buffer[256];
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PRIMARY_DATA);
    }
    
    // Extract model string (words 27-46)
    for (int i = 0; i < 40; i += 2) {
        u16 w = buffer[27 + i/2];
        dev->model[i] = (char)(w >> 8);
        dev->model[i+1] = (char)(w & 0xFF);
    }
    dev->model[40] = '\0';
    
    // Extract sector count (words 60-61 for LBA28)
    dev->sectors = ((u64)buffer[61] << 16) | buffer[60];
    
    dev->present = true;
    return true;
}

bool disk_read_sectors(u64 lba, u32 count, void* buffer) {
    if (!buffer || count == 0 || count > 256) return false;
    if (!primary_master.present) return false;
    
    disk_wait_ready();
    disk_select_drive(0xE0 | ((lba >> 24) & 0x0F));
    
    outb(ATA_PRIMARY_SECTOR_COUNT, (u8)count);
    outb(ATA_PRIMARY_LBA_LOW, (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (u8)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (u8)((lba >> 16) & 0xFF));
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);
    
    // Read data
    u16* buf = (u16*)buffer;
    for (u32 sector = 0; sector < count; sector++) {
        disk_wait_ready();
        
        u8 status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_SR_ERR) {
            return false;
        }
        
        // 256 words per sector
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(ATA_PRIMARY_DATA);
        }
    }
    
    // Cache flush
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    disk_wait_ready();
    
    return true;
}

bool disk_write_sectors(u64 lba, u32 count, const void* buffer) {
    if (!buffer || count == 0 || count > 256) return false;
    if (!primary_master.present) return false;
    
    disk_wait_ready();
    disk_select_drive(0xE0 | ((lba >> 24) & 0x0F));
    
    outb(ATA_PRIMARY_SECTOR_COUNT, (u8)count);
    outb(ATA_PRIMARY_LBA_LOW, (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_LBA_MID, (u8)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_LBA_HIGH, (u8)((lba >> 16) & 0xFF));
    
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);
    
    // Write data
    const u16* buf = (const u16*)buffer;
    for (u32 sector = 0; sector < count; sector++) {
        disk_wait_ready();
        
        // 256 words per sector
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, buf[sector * 256 + i]);
        }
    }
    
    // Cache flush
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);
    disk_wait_ready();
    
    return true;
}

void disk_init(void) {
    vga_puts("Initializing disk...\n");
    
    if (disk_identify(&primary_master)) {
        vga_puts("Primary master: ");
        vga_puts(primary_master.model);
        vga_puts("\n");
        vga_printf("Sectors: %llu\n", primary_master.sectors);
    } else {
        vga_puts("No disk found\n");
    }
}