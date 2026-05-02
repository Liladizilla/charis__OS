#include <kernel/fs.h>
#include <kernel/disk.h>
#include <kernel/string.h>
#include <kernel/vga.h>

static fat16_fs_t fs_state = {0};

static void fs_to_dos_name(const char* src, u8* dest) {
    kmemset(dest, ' ', 11);
    usize i = 0;
    while (src[i] && src[i] != '.' && i < 8) {
        dest[i] = src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i];
        i++;
    }
    if (src[i] == '.') {
        i++;
        usize j = 8;
        while (src[i] && j < 11) {
            dest[j] = src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i];
            i++;
            j++;
        }
    }
}

static u32 fs_next_cluster(u32 cluster) {
    if (!fs_state.fat_cache || cluster >= fs_state.total_clusters) {
        return 0;
    }
    return fs_state.fat_cache[cluster];
}

static bool fs_read_sectors(u32 lba, u32 count, void* buffer) {
    return disk_read_sectors(lba, count, buffer);
}

bool fs_mount(void) {
    if (fs_state.mounted) return true;
    
    u8 sector[512];
    if (!fs_read_sectors(0, 1, sector)) {
        vga_puts("fs: Failed to read boot sector\n");
        return false;
    }
    
    // Verify FAT16 signature
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        vga_puts("fs: Invalid boot sector signature\n");
        return false;
    }
    
    // Copy BPB
    kmemcpy(&fs_state.bpb, sector, sizeof(fat16_bpb_t));
    
    // Calculate filesystem layout
    fs_state.fat_start = fs_state.bpb.reserved_sectors;
    fs_state.root_sectors = (fs_state.bpb.root_entries * 32 + 511) / 512;
    fs_state.root_start = fs_state.fat_start + (fs_state.bpb.fat_count * fs_state.bpb.sectors_per_fat);
    fs_state.data_start = fs_state.root_start + fs_state.root_sectors;
    fs_state.total_clusters = (fs_state.bpb.total_sectors_32 - fs_state.data_start) / fs_state.bpb.sectors_per_cluster;
    
    // Allocate FAT cache
    fs_state.fat_cache = (u16*)kmalloc(fs_state.total_clusters * 2);
    if (!fs_state.fat_cache) {
        vga_puts("fs: Failed to allocate FAT cache\n");
        return false;
    }
    
    // Read FAT table
    if (!fs_read_sectors(fs_state.fat_start, fs_state.bpb.sectors_per_fat, fs_state.fat_cache)) {
        vga_puts("fs: Failed to read FAT\n");
        kfree(fs_state.fat_cache);
        fs_state.fat_cache = NULL;
        return false;
    }
    
    // Initialize file handles
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        fs_state.open_files[i].used = false;
    }
    
    fs_state.mounted = true;
    vga_puts("fs: FAT16 filesystem mounted\n");
    return true;
}

bool fs_list(const char* path, void (*callback)(const char* name, u32 size, u8 attr)) {
    (void)path;
    
    if (!fs_state.mounted || !callback) return false;
    
    u8 sector[512];
    u32 root_sector = 0;
    
    while (root_sector < fs_state.root_sectors) {
        if (!fs_read_sectors(fs_state.root_start + root_sector, 1, sector)) {
            return false;
        }
        
        for (int i = 0; i < 16; i++) {
            fat16_dir_entry_t* entry = (fat16_dir_entry_t*)&sector[i * 32];
            
            if (entry->name[0] == 0) {
                return true; // End of directory
            }
            if (entry->name[0] == 0xE5) {
                continue; // Deleted entry
            }
            if (entry->attr & FAT_ATTR_VOLUME_ID) {
                continue; // Volume label
            }
            
            // Convert DOS name to string
            char name[13];
            for (int j = 0; j < 8; j++) {
                name[j] = entry->name[j] == ' ' ? '\0' : entry->name[j];
            }
            if (entry->name[8] != ' ') {
                name[8] = '.';
                for (int j = 0; j < 3; j++) {
                    name[9 + j] = entry->name[8 + j] == ' ' ? '\0' : entry->name[8 + j];
                }
            }
            name[12] = '\0';
            
            callback(name, entry->file_size, entry->attr);
        }
        root_sector++;
    }
    
    return true;
}

bool fs_open(const char* path, fat16_file_t** out_handle) {
    if (!fs_state.mounted || !path || !out_handle) return false;
    
    // Find free file handle
    fat16_file_t* file = NULL;
    for (int i = 0; i < FAT16_MAX_OPEN; i++) {
        if (!fs_state.open_files[i].used) {
            file = &fs_state.open_files[i];
            break;
        }
    }
    
    if (!file) {
        vga_puts("fs: No free file handles\n");
        return false;
    }
    
    // Convert path to DOS name
    u8 dos_name[11];
    fs_to_dos_name(path, dos_name);
    
    // Search root directory
    u8 sector[512];
    for (u32 sector_idx = 0; sector_idx < fs_state.root_sectors; sector_idx++) {
        if (!fs_read_sectors(fs_state.root_start + sector_idx, 1, sector)) {
            return false;
        }
        
        for (int i = 0; i < 16; i++) {
            fat16_dir_entry_t* entry = (fat16_dir_entry_t*)&sector[i * 32];
            
            if (entry->name[0] == 0 || entry->name[0] == 0xE5) continue;
            if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
            
            if (kmemcmp(entry->name, dos_name, 11) == 0) {
                // Found file
                file->used = true;
                file->cluster = ((u32)entry->cluster_high << 16) | entry->cluster_low;
                file->size = entry->file_size;
                file->position = 0;
                file->attr = entry->attr;
                kstrncpy(file->name, path, FAT16_MAX_FILENAME - 1);
                file->name[FAT16_MAX_FILENAME - 1] = '\0';
                
                *out_handle = file;
                return true;
            }
        }
    }
    
    return false; // File not found
}

bool fs_read(fat16_file_t* file, void* buffer, u32 size, u32* out_read) {
    if (!file || !buffer || !out_read) return false;
    if (!file->used) return false;
    
    *out_read = 0;
    
    // Limit read to file size
    if (file->position + size > file->size) {
        size = file->size - file->position;
    }
    
    if (size == 0) return true;
    
    u8 sector[512];
    u32 bytes_read = 0;
    u32 cluster = file->cluster;
    u32 offset = file->position;
    
    // Skip to correct cluster
    u32 cluster_idx = offset / (fs_state.bpb.sectors_per_cluster * 512);
    for (u32 i = 0; i < cluster_idx && cluster < 0xFFFF; i++) {
        cluster = fs_next_cluster(cluster);
    }
    
    offset = offset % (fs_state.bpb.sectors_per_cluster * 512);
    
    while (bytes_read < size && cluster < 0xFFFF) {
        u32 sector_lba = fs_state.data_start + (cluster - 2) * fs_state.bpb.sectors_per_cluster;
        
        for (u8 s = 0; s < fs_state.bpb.sectors_per_cluster && bytes_read < size; s++) {
            if (!fs_read_sectors(sector_lba + s, 1, sector)) {
                return false;
            }
            
            u32 to_copy = 512;
            if (offset > 0) {
                to_copy = 512 - offset;
                offset = 0;
            }
            if (bytes_read + to_copy > size) {
                to_copy = size - bytes_read;
            }
            
            kmemcpy((u8*)buffer + bytes_read, sector, to_copy);
            bytes_read += to_copy;
        }
        
        cluster = fs_next_cluster(cluster);
    }
    
    file->position += bytes_read;
    *out_read = bytes_read;
    return true;
}

bool fs_write(fat16_file_t* file, const void* buffer, u32 size) {
    // Write not implemented - read-only for now
    (void)file;
    (void)buffer;
    (void)size;
    return false;
}

bool fs_close(fat16_file_t* file) {
    if (!file) return false;
    file->used = false;
    file->name[0] = '\0';
    return true;
}

bool fs_exists(const char* path) {
    fat16_file_t* file;
    bool result = fs_open(path, &file);
    if (result) {
        fs_close(file);
    }
    return result;
}

bool fs_create(const char* path) {
    // Create not implemented - read-only for now
    (void)path;
    return false;
}

bool fs_init(void) {
    vga_puts("Initializing filesystem...\n");
    return fs_mount();
}