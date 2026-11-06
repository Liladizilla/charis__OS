#include <kernel/config.h>
#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/vga.h>
#include <kernel/printf.h>
#include <kernel/serial.h>

static config_entry_t config_entries[CONFIG_MAX_ENTRIES];
static int config_count = 0;
static bool config_persistent = false;

// Helper: trim whitespace
static char* trim(char* str) {
    char* end;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (*str == 0) return str;
    end = str + kstrlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    *(end + 1) = 0;
    return str;
}

// Helper: parse value string into entry
static void parse_value(config_entry_t* entry, char* value_str) {
    value_str = trim(value_str);
    
    // Check for boolean
    if (kstrcmp(value_str, "true") == 0 || kstrcmp(value_str, "false") == 0) {
        entry->type = CONFIG_TYPE_BOOL;
        *(bool*)entry->value = (kstrcmp(value_str, "true") == 0);
        return;
    }
    
    // Check for integer (all digits, optional leading -)
    bool is_int = true;
    char* p = value_str;
    if (*p == '-') p++;
    while (*p) {
        if (*p < '0' || *p > '9') {
            is_int = false;
            break;
        }
        p++;
    }
    if (is_int && *value_str) {
        entry->type = CONFIG_TYPE_INT;
        *(int*)entry->value = katoi(value_str, 10);
        return;
    }
    
    // Default to string
    entry->type = CONFIG_TYPE_STRING;
    kstrncpy(entry->value, value_str, CONFIG_VALUE_MAX - 1);
}

void config_init(void) {
    config_count = 0;
    config_persistent = false;
    vga_puts("Config: Initialized\n");
}

void config_load(const char* path) {
    // First set defaults in case load fails
    config_set_string("theme", "charisos_dark");
    config_set_int("volume", 50);
    config_set_bool("boot_sound", true);
    config_set_int("screen_brightness", 100);
    config_set_bool("net_enabled", true);
    config_set_int("font_style", 0);
    config_set_int("font_scale", 1);
    config_set_int("ui_scale", 100);
    config_set_int("wallpaper", 0);

    config_persistent = false;
    vfs_node_t* node = vfs_resolve(path);
    if (!node) {
        vga_puts("Config: No config file found, using defaults\n");
        return;
    }

    u8 buf[4096];
    int n = node->read ? node->read(node, 0, sizeof(buf) - 1, buf) : -1;
    if (node->close) node->close(node);
    if (n <= 0) {
        vga_puts("Config: Empty or unreadable config file\n");
        return;
    }
    config_persistent = true;
    buf[n] = 0;

    // Parse line by line
    char* line = (char*)buf;
    while (*line) {
        // Find end of line
        char* eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char saved = *eol;
        *eol = 0;

        // Trim and skip empty/comment lines
        char* trimmed = trim(line);
        if (*trimmed && *trimmed != '#') {
            // Find '=' separator
            char* eq = trimmed;
            while (*eq && *eq != '=') eq++;
            if (*eq == '=') {
                *eq = 0;
                char* key = trim(trimmed);
                char* value = trim(eq + 1);
                
                if (*key) {
                    // Check if key already exists
                    for (int i = 0; i < config_count; i++) {
                        if (kstrcmp(config_entries[i].key, key) == 0) {
                            parse_value(&config_entries[i], value);
                            goto next_line;
                        }
                    }
                    // New entry
                    if (config_count < CONFIG_MAX_ENTRIES) {
                        kstrncpy(config_entries[config_count].key, key, CONFIG_KEY_MAX - 1);
                        parse_value(&config_entries[config_count], value);
                        config_count++;
                    }
                }
            }
        }

next_line:
        *eol = saved;
        if (*eol == '\r' && *(eol + 1) == '\n') eol++;
        if (*eol == '\n' && *(eol + 1) == '\r') eol++;
        line = eol + 1;
    }

    vga_printf("Config: Loaded %d entries from %s\n", config_count, path);
    serial_printf("CONFIG_LOADED entries=%d font_style=%d font_scale=%d ui_scale=%d\n",
                  config_count, config_get_int("font_style", 0),
                  config_get_int("font_scale", 1), config_get_int("ui_scale", 100));
}

bool config_save(const char* path) {
    /* The current FAT32 implementation supports root-directory 8.3 files,
     * not nested directories. Refuse paths it cannot safely represent. */
    if (!path || path[0] != '/' || !path[1]) return false;
    for (const char* p = path + 1; *p; p++) {
        if (*p == '/' || *p == '\\') return false;
    }

    char buf[4096];
    int pos = 0;

    for (int i = 0; i < config_count; i++) {
        char* k = config_entries[i].key;
        while (*k && pos < 4000) buf[pos++] = *k++;
        if (pos >= 4000) break;
        buf[pos++] = '=';

        if (config_entries[i].type == CONFIG_TYPE_BOOL) {
            const char* val = *(bool*)config_entries[i].value ? "true" : "false";
            while (*val && pos < 4000) buf[pos++] = *val++;
        } else if (config_entries[i].type == CONFIG_TYPE_INT) {
            char int_buf[32];
            kitoa(*(int*)config_entries[i].value, int_buf, 10);
            char* v = int_buf;
            while (*v && pos < 4000) buf[pos++] = *v++;
        } else {
            char* v = config_entries[i].value;
            while (*v && pos < 4000) buf[pos++] = *v++;
        }
        if (pos >= 4000) break;
        buf[pos++] = '\n';
    }

    vfs_node_t* node = vfs_resolve(path);
    if (!node) {
        /* Create the configuration file on a writable, mounted FAT32 volume.
         * fs_create() safely fails when no supported disk is available. */
        if (fs_create(path) == 0) node = vfs_resolve(path);
    }

    bool saved = node && node->write &&
                 node->write(node, 0, (u64)pos, (const u8*)buf) == pos;
    if (node && node->close) node->close(node);

    config_persistent = saved;
    serial_printf("CONFIG_SAVE %s font_style=%d font_scale=%d ui_scale=%d\n",
                  saved ? "OK" : "UNAVAILABLE",
                  config_get_int("font_style", 0),
                  config_get_int("font_scale", 1), config_get_int("ui_scale", 100));
    if (saved) {
        vga_printf("Config: Saved %d entries to %s\n", config_count, path);
    } else {
        vga_puts("Config: No writable FAT32 config storage; settings remain session-only\n");
    }
    return saved;
}

bool config_is_persistent(void) {
    return config_persistent;
}

int config_get_int(const char* key, int default_value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0 && config_entries[i].type == CONFIG_TYPE_INT) {
            return *(int*)config_entries[i].value;
        }
    }
    return default_value;
}

const char* config_get_string(const char* key, const char* default_value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0 && config_entries[i].type == CONFIG_TYPE_STRING) {
            return config_entries[i].value;
        }
    }
    return default_value;
}

bool config_get_bool(const char* key, bool default_value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0 && config_entries[i].type == CONFIG_TYPE_BOOL) {
            return *(bool*)config_entries[i].value;
        }
    }
    return default_value;
}

void config_set_int(const char* key, int value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0) {
            *(int*)config_entries[i].value = value;
            config_entries[i].type = CONFIG_TYPE_INT;
            return;
        }
    }
    if (config_count < CONFIG_MAX_ENTRIES) {
        kstrncpy(config_entries[config_count].key, key, CONFIG_KEY_MAX - 1);
        *(int*)config_entries[config_count].value = value;
        config_entries[config_count].type = CONFIG_TYPE_INT;
        config_count++;
    }
}

void config_set_string(const char* key, const char* value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0) {
            kstrncpy(config_entries[i].value, value, CONFIG_VALUE_MAX - 1);
            config_entries[i].type = CONFIG_TYPE_STRING;
            return;
        }
    }
    if (config_count < CONFIG_MAX_ENTRIES) {
        kstrncpy(config_entries[config_count].key, key, CONFIG_KEY_MAX - 1);
        kstrncpy(config_entries[config_count].value, value, CONFIG_VALUE_MAX - 1);
        config_entries[config_count].type = CONFIG_TYPE_STRING;
        config_count++;
    }
}

void config_set_bool(const char* key, bool value) {
    for (int i = 0; i < config_count; i++) {
        if (kstrcmp(config_entries[i].key, key) == 0) {
            *(bool*)config_entries[i].value = value;
            config_entries[i].type = CONFIG_TYPE_BOOL;
            return;
        }
    }
    if (config_count < CONFIG_MAX_ENTRIES) {
        kstrncpy(config_entries[config_count].key, key, CONFIG_KEY_MAX - 1);
        *(bool*)config_entries[config_count].value = value;
        config_entries[config_count].type = CONFIG_TYPE_BOOL;
        config_count++;
    }
}