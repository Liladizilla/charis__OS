#include <kernel/ata.h>
#include <kernel/printf.h>
#include <kernel/vga.h>
#include <kernel/io.h>
#include <kernel/timer.h>
#include <kernel/serial.h>

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
#define ATA_TIMEOUT_MS 30

static ata_device_info_t ata_primary_master;
static bool ata_present = false;

bool ata_is_present(void) {
    return ata_present;
}

bool ata_get_device_info(ata_device_info_t* output) {
    if (!output || !ata_primary_master.detected) return false;
    const u8* source = (const u8*)&ata_primary_master;
    u8* destination = (u8*)output;
    for (u32 i = 0; i < sizeof(ata_primary_master); i++)
        destination[i] = source[i];
    return true;
}

static bool ata_lba_is_addressable(u32 lba) {
    return ata_present &&
           ata_device_lba28_is_addressable(&ata_primary_master, lba);
}

static bool ata_status_is_absent(void) {
    u8 status = inb(ATA_STATUS);
    return status == 0 || status == 0xFF;
}

static bool ata_wait_bsy(void) {
    u64 start = timer_get_ms();
    while (1) {
        if (timer_get_ms() - start >= ATA_TIMEOUT_MS) return false;
        u8 status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF) return false;
        if (!(status & ATA_STATUS_BSY)) return true;
    }
}

static bool ata_wait_drq(void) {
    u64 start = timer_get_ms();
    while (1) {
        if (timer_get_ms() - start >= ATA_TIMEOUT_MS) return false;
        u8 status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF || (status & ATA_STATUS_ERR)) return false;
        if (status & ATA_STATUS_DRQ) return true;
    }
}

bool ata_read_sector(u32 lba, void* buffer) {
    if (!buffer || !ata_lba_is_addressable(lba) || !ata_wait_bsy())
        return false;

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
    if (!buffer || !ata_lba_is_addressable(lba) || !ata_wait_bsy())
        return false;

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
    for (u32 i = 0; i < sizeof(ata_primary_master); i++)
        ((u8*)&ata_primary_master)[i] = 0;
    ata_present = false;

    /* IDENTIFY DEVICE is read-only. This pass records identity and capacity;
     * it never writes to the device or assumes sector zero is a filesystem. */
    outb(ATA_DRIVE, 0xA0); /* Primary master. */
    io_delay();
    outb(ATA_SECTOR_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    io_delay();

    if (!ata_wait_bsy() || ata_status_is_absent() || !ata_wait_drq()) {
        vga_puts("ATA: No primary-master ATA disk detected\n");
        serial_puts("ATA: no primary-master ATA disk detected\n");
        return;
    }

    u16 identify[256];
    for (u32 i = 0; i < 256u; i++)
        identify[i] = inw(ATA_DATA);

    if (!ata_identify_decode(identify, &ata_primary_master)) {
        vga_puts("ATA: IDENTIFY data invalid or unsupported\n");
        serial_puts("ATA: IDENTIFY data invalid or unsupported\n");
        return;
    }

    ata_present = ata_primary_master.io_supported;

    kprintf("ATA: %s, %llu sectors, %u-byte logical sectors\n",
            ata_primary_master.model[0] ? ata_primary_master.model : "Unnamed ATA device",
            (unsigned long long)ata_primary_master.capacity_sectors,
            ata_primary_master.logical_sector_bytes);

    if (ata_primary_master.lba48_supported) {
        serial_puts("ATA: LBA48 capable; current PIO access remains limited to LBA28\n");
    }

    if (!ata_primary_master.io_supported) {
        vga_puts("ATA: Device detected but unsupported by current PIO path\n");
        serial_puts("ATA: device inventoried but not readable by current PIO path\n");
        return;
    }

    vga_puts("ATA initialized (primary-master LBA28 PIO)\n");
    serial_puts("ATA: primary-master device ready for 512-byte LBA28 PIO\n");
}
