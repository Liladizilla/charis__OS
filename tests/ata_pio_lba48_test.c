#include <kernel/ata.h>
#include <kernel/io.h>

#include <stdio.h>
#include <string.h>

#define ATA_TEST_DATA       0x1F0u
#define ATA_TEST_SECTOR_COUNT 0x1F2u
#define ATA_TEST_LBA_LOW    0x1F3u
#define ATA_TEST_LBA_MID    0x1F4u
#define ATA_TEST_LBA_HIGH   0x1F5u
#define ATA_TEST_DRIVE      0x1F6u
#define ATA_TEST_STATUS     0x1F7u
#define ATA_TEST_COMMAND    0x1F7u
#define ATA_TEST_READ_EXT   0x24u
#define ATA_TEST_IDENTIFY   0xECu

typedef struct {
    u16 port;
    u8 value;
} port_write_t;

static u16 identify_words[256];
static u32 identify_index;
static bool identify_data_active;
static port_write_t writes[32];
static u32 write_count;
static u32 io_delay_count;
static u32 failures;

static void expect(bool condition, const char* message) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

void outb(u16 port, u8 value) {
    if (write_count < sizeof(writes) / sizeof(writes[0])) {
        writes[write_count].port = port;
        writes[write_count].value = value;
        write_count++;
    }
    if (port == ATA_TEST_COMMAND && value == ATA_TEST_IDENTIFY) {
        identify_index = 0u;
        identify_data_active = true;
    }
}

u8 inb(u16 port) {
    return port == ATA_TEST_STATUS ? 0x48u : 0u; /* DRDY | DRQ */
}

u16 inw(u16 port) {
    if (port == ATA_TEST_DATA && identify_data_active &&
        identify_index < 256u) {
        u16 value = identify_words[identify_index++];
        if (identify_index == 256u) identify_data_active = false;
        return value;
    }
    return 0xA55Au;
}

void outw(u16 port, u16 value) {
    (void)port;
    (void)value;
}

void io_delay(void) { io_delay_count++; }
void io_wait(void) { }
u64 timer_get_ms(void) { return 0u; }

void vga_puts(const char* text) { (void)text; }
void serial_puts(const char* text) { (void)text; }
void kprintf(const char* format, ...) { (void)format; }

static void prepare_identify(void) {
    memset(identify_words, 0, sizeof(identify_words));
    identify_words[0] = 0x0040u; /* ATA fixed disk. */
    identify_words[49] = 1u << 9; /* LBA supported. */
    identify_words[60] = 0xFFFFu;
    identify_words[61] = 0x0FFFu; /* LBA28 window present. */
    identify_words[83] = 0x4400u; /* Valid command word, LBA48 supported. */
    identify_words[86] = 0x4400u; /* LBA48 enabled. */
    identify_words[100] = 0x9ABCu;
    identify_words[101] = 0x5678u;
    identify_words[102] = 0x1234u;
    identify_words[103] = 0u;
}

static void expect_write(u32 index, u16 port, u8 value, const char* message) {
    expect(index < write_count &&
           writes[index].port == port &&
           writes[index].value == value, message);
}

int main(void) {
    const u64 capacity = 0x0000123456789ABCULL;
    const u64 lba = capacity - 1u;
    u16 sector[ATA_SECTOR_BYTES / sizeof(u16)];

    prepare_identify();
    write_count = 0u;
    io_delay_count = 0u;
    ata_init();

    expect(ata_device_lba48_is_addressable(&(ata_device_info_t){
               .detected = true,
               .lba48_supported = true,
               .io_supported = true,
               .logical_sector_bytes = 512u,
               .capacity_sectors = capacity
           }, lba),
           "last reported LBA48 sector passes address validation");
    expect(!ata_device_lba48_is_addressable(&(ata_device_info_t){
               .detected = true,
               .lba48_supported = true,
               .io_supported = true,
               .logical_sector_bytes = 512u,
               .capacity_sectors = capacity
           }, capacity),
           "first sector beyond device capacity is rejected");

    write_count = 0u;
    memset(sector, 0, sizeof(sector));
    expect(ata_read_sector64(lba, sector),
           "single-sector READ SECTORS EXT succeeds with a ready mock device");
    expect(write_count == 10u,
           "LBA48 command writes exactly the bounded ten task-file values");

    /* ATA LBA48 task files are programmed high-order bytes first, then
     * low-order bytes. The selected device is primary master, LBA mode. */
    expect_write(0u, ATA_TEST_DRIVE, 0x40u,
                 "select primary master with LBA mode and reserved bits clear");
    expect_write(1u, ATA_TEST_SECTOR_COUNT, 0u,
                 "write high sector-count byte first");
    expect_write(2u, ATA_TEST_LBA_LOW, 0x56u,
                 "write LBA bits 31:24 before low-order address bytes");
    expect_write(3u, ATA_TEST_LBA_MID, 0x34u,
                 "write LBA bits 39:32 before low-order address bytes");
    expect_write(4u, ATA_TEST_LBA_HIGH, 0x12u,
                 "write LBA bits 47:40 before low-order address bytes");
    expect_write(5u, ATA_TEST_SECTOR_COUNT, 1u,
                 "write low sector-count byte second");
    expect_write(6u, ATA_TEST_LBA_LOW, 0xBBu,
                 "write LBA bits 7:0");
    expect_write(7u, ATA_TEST_LBA_MID, 0x9Au,
                 "write LBA bits 15:8");
    expect_write(8u, ATA_TEST_LBA_HIGH, 0x78u,
                 "write LBA bits 23:16");
    expect_write(9u, ATA_TEST_COMMAND, ATA_TEST_READ_EXT,
                 "issue READ SECTORS EXT only after the complete address");
    expect(sector[0] == 0xA55Au &&
           sector[(ATA_SECTOR_BYTES / sizeof(u16)) - 1u] == 0xA55Au,
           "exactly one 512-byte sector is transferred into the caller buffer");
    expect(io_delay_count >= 2u,
           "device selection and command issue are separated by I/O delays");

    write_count = 0u;
    expect(!ata_read_sector64(capacity, sector),
           "read beyond reported capacity is rejected");
    expect(write_count == 0u,
           "invalid LBA requests issue no task-file writes");

    write_count = 0u;
    expect(!ata_read_sector64(1ULL << 48, sector),
           "address above the 48-bit ATA limit is rejected");
    expect(write_count == 0u,
           "out-of-width LBA requests issue no task-file writes");

    if (failures) {
        fprintf(stderr, "ATA LBA48 PIO: %u failure(s)\n", failures);
        return 1;
    }
    puts("ATA LBA48 PIO: all checks passed");
    return 0;
}
