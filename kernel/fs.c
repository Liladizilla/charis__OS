#include <kernel/fs.h>
#include <kernel/ata.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

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
    u16 mod_time;
    u16 mod_date;
    u16 cluster_low;
    u32 size;
} __attribute__((packed)) fat32_dir_entry_t;

static fat32_boot_sector_t boot_sector;
static u32 fat_start;
static u32 data_start;
static u32 root_dir_sectors;
static u32 total_clusters;

/* Set only when a valid FAT32 volume was found. Without this, fs_open() would
 * walk a garbage FAT chain on machines that have no bootable disk attached,
 * which hangs the kernel during config_load() at boot. */
static bool fs_mounted = false;

static char fat_upper(char value) {
    if (value >= 'a' && value <= 'z') return (char)(value - 'a' + 'A');
    return value;
}

static bool fat32_name_matches(const char* path, const char raw_name[11]) {
    if (!path || !path[0]) return false;

    usize pos = 0;
    usize base_length = 0;
    while (path[pos] && path[pos] != '.') {
        if (base_length == 8 || fat_upper(path[pos]) != raw_name[base_length]) return false;
        pos++;
        base_length++;
    }
    if (!base_length) return false;
    while (base_length < 8) {
        if (raw_name[base_length] != ' ') return false;
        base_length++;
    }

    usize extension_length = 0;
    if (path[pos] == '.') {
        pos++;
        while (path[pos]) {
            if (extension_length == 3 || fat_upper(path[pos]) != raw_name[8 + extension_length]) return false;
            pos++;
            extension_length++;
        }
    }
    if (path[pos]) return false;
    while (extension_length < 3) {
        if (raw_name[8 + extension_length] != ' ') return false;
        extension_length++;
    }
    return true;
}

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
    fs_mounted = false;
    u8 sector_buffer[FAT32_SECTOR_SIZE];

    if (!ata_read_sector(0, sector_buffer)) {
        vga_puts("FS: Failed to read boot sector\n");
        return;
    }
    kmemcpy(&boot_sector, sector_buffer, sizeof(boot_sector));

    if (kstrncmp(boot_sector.fs_type, "FAT32", 5) != 0) {
        vga_puts("FS: Not FAT32 (no FAT32 volume found)\n");
        return;
    }

    /* Reject a boot sector whose geometry would produce nonsense cluster maths. */
    if (boot_sector.sectors_per_cluster == 0 ||
        boot_sector.num_fats == 0 ||
        boot_sector.fat_size_32 == 0) {
        vga_puts("FS: Invalid FAT32 geometry\n");
        return;
    }

    fat_start = boot_sector.reserved_sectors;
    root_dir_sectors = ((boot_sector.root_entries * 32) + (FAT32_SECTOR_SIZE - 1)) / FAT32_SECTOR_SIZE;
    data_start = boot_sector.reserved_sectors + (boot_sector.num_fats * boot_sector.fat_size_32) + root_dir_sectors;

    if (boot_sector.total_sectors_32 <= data_start) {
        vga_puts("FS: Invalid FAT32 geometry (data_start past end of volume)\n");
        return;
    }

    total_clusters = (boot_sector.total_sectors_32 - data_start) / boot_sector.sectors_per_cluster;

    if (boot_sector.root_cluster < 2 || boot_sector.root_cluster >= total_clusters + 2) {
        vga_puts("FS: Invalid root cluster\n");
        return;
    }

    fs_mounted = true;
    vga_puts("FAT32 initialized\n");
}

int fs_open(const char* path, file_t* file) {
    if (!fs_mounted) return -1;
    if (!path || !file) return -1;
    while (path[0] == '/') path++;

    // Simple: assume root directory, find file by name
    u32 cluster = boot_sector.root_cluster;
    u8 buffer[512];

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 sector = cluster_to_sector(cluster);
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (!ata_read_sector(sector + s, buffer)) return -1;
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0) break;
                if ((u8)entries[i].name[0] == 0xE5) continue; // Deleted
                if (entries[i].attr & 0x18) continue; // Directory or volume label
                if (fat32_name_matches(path, entries[i].name)) {
                    file->cluster = (entries[i].cluster_high << 16) | entries[i].cluster_low;
                    file->size = entries[i].size;
                    file->pos = 0;
                    file->dir_cluster = cluster;
                    file->dir_sector = s;
                    file->dir_index = i;
                    return 0;
                }
            }
        }
        cluster = next_cluster(cluster);
    }
    return -1;
}

int fs_read(file_t* file, void* buffer, usize size) {
    if (!fs_mounted) return -1;
    if (!file || !buffer) return -1;
    if (file->pos >= file->size) return 0;
    if (size > file->size - file->pos) size = file->size - file->pos;

    usize bytes_read = 0;
    u32 cluster = file->cluster;
    u32 cluster_size = boot_sector.sectors_per_cluster * FAT32_SECTOR_SIZE;
    u32 clusters_to_skip = file->pos / cluster_size;
    while (clusters_to_skip--) {
        cluster = next_cluster(cluster);
        if (cluster < 2 || cluster >= 0x0FFFFFF8) return -1;
    }

    u32 offset = file->pos % cluster_size;
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

        if (offset >= cluster_size) {
            cluster = next_cluster(cluster);
            offset = 0;
            sector_offset = 0;
        } else {
            sector_offset = offset / FAT32_SECTOR_SIZE;
        }
    }
    return bytes_read;
}

/* ------------------------------------------------------------------ */
/* FAT helpers                                                         */
/* ------------------------------------------------------------------ */

static u32 fat_get(u32 cluster) {
    u32 sector = fat_start + (cluster * 4 / FAT32_SECTOR_SIZE);
    u32 offset = (cluster * 4) % FAT32_SECTOR_SIZE;
    u8 buffer[FAT32_SECTOR_SIZE];
    if (!ata_read_sector(sector, buffer)) return 0x0FFFFFFF;
    u32 entry;
    kmemcpy(&entry, buffer + offset, 4);
    return entry & 0x0FFFFFFF;
}

static bool fat_set(u32 cluster, u32 value) {
    u32 sector = fat_start + (cluster * 4 / FAT32_SECTOR_SIZE);
    u32 offset = (cluster * 4) % FAT32_SECTOR_SIZE;
    u8 buffer[FAT32_SECTOR_SIZE];
    if (!ata_read_sector(sector, buffer)) return false;
    value &= 0x0FFFFFFF;
    kmemcpy(buffer + offset, &value, 4);
    if (!ata_write_sector(sector, buffer)) return false;

    /* Update the second FAT copy as well. A mismatch here is what `fsck.vfat`
     * reports as " FATs differ" and what Windows offers to "fix". */
    if (boot_sector.num_fats >= 2) {
        u32 sector2 = sector + boot_sector.fat_size_32;
        if (!ata_read_sector(sector2, buffer)) return false;
        kmemcpy(buffer + offset, &value, 4);
        if (!ata_write_sector(sector2, buffer)) return false;
    }
    return true;
}

static u32 fat_alloc_cluster(void) {
    u32 total = total_clusters + 2;
    for (u32 c = 2; c < total; c++) {
        if (fat_get(c) == 0x00000000) {
            if (!fat_set(c, 0x0FFFFFFF)) return 0;
            return c;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Write / create                                                      */
/* ------------------------------------------------------------------ */

static bool fs_update_dir_entry(file_t* file) {
    if (!file->dir_cluster || file->dir_index < 0) return false;
    u32 sector = cluster_to_sector(file->dir_cluster) + file->dir_sector;
    u8 buffer[FAT32_SECTOR_SIZE];
    if (!ata_read_sector(sector, buffer)) return false;
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    entries[file->dir_index].size = file->size;
    return ata_write_sector(sector, buffer);
}

static bool fs_extend(file_t* file, usize needed_clusters) {
    u32 cluster_size = boot_sector.sectors_per_cluster * FAT32_SECTOR_SIZE;
    u32 current_clusters = (file->size + cluster_size - 1) / cluster_size;
    u32 target_clusters = ((file->pos + needed_clusters) + cluster_size - 1) / cluster_size;
    if (target_clusters <= current_clusters) return true;

    u32 prev = file->cluster;
    if (!prev) {
        prev = fat_alloc_cluster();
        if (!prev) return false;
        file->cluster = prev;
    }
    for (u32 i = current_clusters; i < target_clusters; i++) {
        u32 next = fat_alloc_cluster();
        if (!next) return false;
        if (!fat_set(prev, next)) return false;
        prev = next;
    }
    return true;
}

int fs_write(file_t* file, const void* buffer, usize size) {
    if (!fs_mounted || !file || !buffer || !size) return -1;

    const u8* src = (const u8*)buffer;
    usize written = 0;
    u32 cluster_size = boot_sector.sectors_per_cluster * FAT32_SECTOR_SIZE;

    if (!fs_extend(file, size)) return -1;

    while (written < size) {
        u32 cluster = file->cluster;
        u32 clusters_to_skip = file->pos / cluster_size;
        while (clusters_to_skip--) {
            u32 next = fat_get(cluster);
            if (next < 2 || next >= 0x0FFFFFF8) return -1;
            cluster = next;
        }

        u32 offset = file->pos % cluster_size;
        u32 sector_offset = offset / FAT32_SECTOR_SIZE;

        while (written < size) {
            u32 sector = cluster_to_sector(cluster) + sector_offset;
            u8 sec_buffer[FAT32_SECTOR_SIZE];

            /* Only zero-fill if we start mid-sector; otherwise we will
             * overwrite the full sector below anyway. */
            if (offset % FAT32_SECTOR_SIZE == 0) {
                kmemset(sec_buffer, 0, FAT32_SECTOR_SIZE);
            } else {
                if (!ata_read_sector(sector, sec_buffer)) return -1;
            }

            usize to_write = size - written;
            u32 sector_remain = FAT32_SECTOR_SIZE - (offset % FAT32_SECTOR_SIZE);
            if (to_write > sector_remain) to_write = sector_remain;

            kmemcpy(sec_buffer + (offset % FAT32_SECTOR_SIZE), src + written, to_write);

            if (!ata_write_sector(sector, sec_buffer)) return -1;

            written += to_write;
            file->pos += to_write;
            offset += to_write;

            if (file->pos > file->size) file->size = file->pos;

            if (offset >= cluster_size) {
                u32 next = fat_get(cluster);
                if (next < 2 || next >= 0x0FFFFFF8) {
                    /* Extend the chain by one more cluster. */
                    next = fat_alloc_cluster();
                    if (!next) return written;
                    if (!fat_set(cluster, next)) return written;
                }
                cluster = next;
                offset = 0;
                sector_offset = 0;
            } else {
                sector_offset = offset / FAT32_SECTOR_SIZE;
            }
        }
    }

    fs_update_dir_entry(file);
    return written;
}

int fs_create(const char* path) {
    if (!fs_mounted || !path || !path[0]) return -1;

    while (path[0] == '/') path++;

    u8 buffer[FAT32_SECTOR_SIZE];
    u32 cluster = boot_sector.root_cluster;
    int free_index = -1;
    u32 free_sector = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 sector = cluster_to_sector(cluster);
        for (u32 s = 0; s < boot_sector.sectors_per_cluster; s++) {
            if (!ata_read_sector(sector + s, buffer)) return -1;
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) {
                    free_index = i;
                    free_sector = sector + s;
                    goto found_free;
                }
                if ((u8)entries[i].name[0] == 0xE5 && free_index < 0) {
                    free_index = i;
                    free_sector = sector + s;
                }
                if (fat32_name_matches(path, entries[i].name)) return -1;
            }
        }
        cluster = next_cluster(cluster);
    }

found_free:
    if (free_index < 0) return -1;

    u32 new_cluster = fat_alloc_cluster();
    if (!new_cluster) return -1;

    if (!ata_read_sector(free_sector, buffer)) return -1;
    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
    fat32_dir_entry_t* e = &entries[free_index];
    kmemset(e, 0, sizeof(*e));

    /* Build 8.3 name from the supplied path. */
    usize pos = 0;
    for (int i = 0; i < 8 && path[pos] && path[pos] != '.'; i++, pos++) {
        e->name[i] = (char)fat_upper(path[pos]);
    }
    for (usize i = pos; i < 8; i++) e->name[i] = ' ';
    e->name[8] = ' '; e->name[9] = ' '; e->name[10] = ' ';
    if (path[pos] == '.') {
        pos++;
        for (int i = 0; i < 3 && path[pos]; i++, pos++) {
            e->name[8 + i] = (char)fat_upper(path[pos]);
        }
    }
    e->attr = 0x20; /* archive */
    e->cluster_low = (u16)(new_cluster & 0xFFFF);
    e->cluster_high = (u16)((new_cluster >> 16) & 0xFFFF);
    e->size = 0;

    if (!ata_write_sector(free_sector, buffer)) {
        fat_set(new_cluster, 0x00000000);
        return -1;
    }
    return 0;
}

int fs_close(file_t* file) {
    (void)file;
    return 0;
}

/* Boot-time regression: proves write, extend, and read-back all work. */
void fs_self_test(void) {
    static const char* msg = "FAT32 WRITE OK";
    file_t file;
    char buf[64];

    if (fs_create("/WRITE.TXT") != 0) {
        kprintf("FS: WRITE FAIL (create)\n");
        return;
    }
    if (fs_open("/WRITE.TXT", &file) != 0) {
        kprintf("FS: WRITE FAIL (open after create)\n");
        return;
    }
    int w = fs_write(&file, msg, kstrlen(msg));
    if (w != (int)kstrlen(msg)) {
        kprintf("FS: WRITE FAIL (short write=%d)\n", w);
        return;
    }
    fs_close(&file);

    if (fs_open("/WRITE.TXT", &file) != 0) {
        kprintf("FS: WRITE FAIL (reopen)\n");
        return;
    }
    int r = fs_read(&file, buf, sizeof(buf) - 1);
    if (r <= 0) {
        kprintf("FS: WRITE FAIL (read back=%d)\n", r);
        return;
    }
    buf[r] = 0;
    fs_close(&file);

    if (kstrcmp(buf, msg) == 0) {
        kprintf("FS: WRITE OK [%s]\n", buf);
    } else {
        kprintf("FS: WRITE MISMATCH [got:%s expected:%s]\n", buf, msg);
    }
}