#include <kernel/fs.h>
#include <kernel/ata.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/vga.h>

#define FAT32_SECTOR_SIZE 512
#define FAT32_MAX_FILES 128

typedef struct {
    u8 jump[3];
    char oem[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 num_fats;
    u16 root_entries;
    u16 total_sectors_16;
    u8 media;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 heads;
    u32 hidden_sectors;
    u32 total_sectors_32;
    u32 fat_size_32;
    u16 flags;
    u16 version;
    u32 root_cluster;
    u16 fs_info_sector;
    u16 backup_boot_sector;
    u8 reserved[12];
    u8 drive_number;
    u8 reserved1;
    u8 boot_signature;
    u32 volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) fat32_boot_sector_t;

typedef struct {
    char name[11];
    u8 attr;
    u8 nt_res;
    u8 create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 cluster_low;
    u16 mod_time;
    u16 mod_date;
    u16 cluster;
    u32 size;
} __attribute__((packed)) fat32_dir_entry_t;

static fat32_boot_sector_t boot_sector;
static u32 fat_start;
static u32 data_start;
static u32 root_dir_sectors;
static u32 total_clusters;

static u32 cluster_to_sector(u32 cluster) {
    return data_start + (cluster - 2) * boot_sector.sectors_per_cluster;
}

static u32 next_cluster(u32 cluster) {
    u32 fat_sector = fat_start + (cluster * 4 / FAT32_SECTOR_SIZE);
    u32 offset = (cluster * 4) % FAT32_SECTOR_SIZE;
    u8 buffer[512];
    if (!ata_read_sector(fat_sector, buffer)) return 0xFFFFFFFF;
    u32 next = *(u32*)&buffer[offset] & 0x0FFFFFFF;
    return next >= 0x0FFFFFF8 ? 0xFFFFFFFF : next;
}

void fs_init(void) {
    if (!ata_read_sector(0, (void*)&boot_sector)) {
        vga_puts("FS: Failed to read boot sector\n");
        return;
    }

    if (kstrncmp(boot_sector.fs_type, "FAT32", 5) != 0) {
        vga_puts("FS: Not FAT32\n");
        return;
    }

    fat_start = boot_sector.reserved_sectors;
    root_dir_sectors = ((boot_sector.root_entries * 32) + (FAT32_SECTOR_SIZE - 1)) / FAT32_SECTOR_SIZE;
    data_start = boot_sector.reserved_sectors + (boot_sector.num_fats * boot_sector.fat_size_32) + root_dir_sectors;
    total_clusters = (boot_sector.total_sectors_32 - data_start) / boot_sector.sectors_per_cluster;

    vga_puts("FAT32 initialized\n");
}

int fs_open(const char* path, file_t* file) {
    // Simple: assume root directory, find file by name
    u32 cluster = boot_sector.root_cluster;
    u8 buffer[512];

    while (cluster < 0x0FFFFFF8) {
        u32 sector = cluster_to_sector(cluster);
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (!ata_read_sector(sector + s, buffer)) return -1;
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0) break;
                if (entries[i].name[0] == 0xE5) continue; // Deleted
                if (entries[i].attr & 0x0F) continue; // Not file
                char name[12];
                kmemcpy(name, entries[i].name, 11);
                name[11] = 0;
                if (kstrcmp(name, path) == 0) {
                    file->cluster = (entries[i].cluster_high << 16) | entries[i].cluster_low;
                    file->size = entries[i].size;
                    file->pos = 0;
                    return 0;
                }
            }
        }
        cluster = next_cluster(cluster);
    }
    return -1;
}

int fs_read(file_t* file, void* buffer, usize size) {
    if (file->pos >= file->size) return 0;
    if (file->pos + size > file->size) size = file->size - file->pos;

    usize bytes_read = 0;
    u32 cluster = file->cluster;
    u32 offset = file->pos % (boot_sector.sectors_per_cluster * FAT32_SECTOR_SIZE);
    u32 sector_offset = offset / FAT32_SECTOR_SIZE;

    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        u32 sector = cluster_to_sector(cluster) + sector_offset;
        u8 sec_buffer[512];
        if (!ata_read_sector(sector, sec_buffer)) return -1;

        usize to_read = size - bytes_read;
        if (to_read > FAT32_SECTOR_SIZE - (offset % FAT32_SECTOR_SIZE)) {
            to_read = FAT32_SECTOR_SIZE - (offset % FAT32_SECTOR_SIZE);
        }

        kmemcpy((u8*)buffer + bytes_read, sec_buffer + (offset % FAT32_SECTOR_SIZE), to_read);
        bytes_read += to_read;
        file->pos += to_read;
        offset += to_read;

        if (offset >= boot_sector.sectors_per_cluster * FAT32_SECTOR_SIZE) {
            cluster = next_cluster(cluster);
            offset = 0;
            sector_offset = 0;
        } else {
            sector_offset = offset / FAT32_SECTOR_SIZE;
        }
    }
    return bytes_read;
}

int fs_close(file_t* file) {
    (void)file;
    return 0;
}