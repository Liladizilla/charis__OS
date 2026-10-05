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

/* Keep polling finite: each port read can exit to the host under KVM. */
#define ATA_WAIT_LIMIT 5000

static bool ata_status_is_absent(void) {
    u8 status = inb(ATA_STATUS);
    return status == 0 || status == 0xFF;
}

static bool ata_wait_bsy(void) {
    for (u32 i = 0; i < ATA_WAIT_LIMIT; i++) {
        u8 status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF) return false;
        if (!(status & ATA_STATUS_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(void) {
    for (u32 i = 0; i < ATA_WAIT_LIMIT; i++) {
        u8 status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_STATUS_ERR)) return false;
        if (status & ATA_STATUS_DRQ) return true;
    }
    return false;
}

bool ata_read_sector(u32 lba, void* buffer) {
    if (!buffer || !ata_wait_bsy()) return false;

    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_READ);

    /* Standard ATA PIO: give the device ~400 ns to accept the command
     * before polling BSY. Without this, QEMU's TCG mode sometimes sees
     * BSY still set and the read fails with "post-busy" / "no-drq". */
    io_delay();

    if (!ata_wait_bsy()) return false;
    if (ata_status_is_absent()) return false;
    if (inb(ATA_STATUS) & ATA_STATUS_ERR) return false;
    if (!ata_wait_drq()) return false;

    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        ((u16*)buffer)[i] = inw(ATA_DATA);
    }

    return true;
}

bool ata_write_sector(u32 lba, const void* buffer) {
    if (!buffer || !ata_wait_bsy()) return false;

    outb(ATA_SECTOR_COUNT, 1);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_COMMAND, ATA_CMD_WRITE);

    if (!ata_wait_bsy()) return false;
    if (ata_status_is_absent()) return false;
    if (inb(ATA_STATUS) & ATA_STATUS_ERR) return false;
    if (!ata_wait_drq()) return false;

    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
        outw(ATA_DATA, ((u16*)buffer)[i]);
    }

    // Wait for write to complete
    return ata_wait_bsy();
}

void ata_init(void) {
    // Identify drive
    outb(ATA_DRIVE, 0xA0); // Master drive
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    bool ready = ata_wait_bsy();
    u8 status = inb(ATA_STATUS);
    if (!ready || status == 0 || status == 0xFF || (status & ATA_STATUS_ERR)) {
        vga_puts("ATA: No drive detected\n");
        return;
    }

    // IDENTIFY leaves a 512-byte data phase pending. Drain it before the
    // first READ SECTORS command or the following PIO read consumes this data.
    if (status & ATA_STATUS_DRQ) {
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) (void)inw(ATA_DATA);
    }

    vga_puts("ATA initialized\n");
}