#include <kernel/config.h>
#include <kernel/vfs.h>
#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

static config_entry_t config_entries[CONFIG_MAX_ENTRIES];
static u32 config_count;

static char* trim(char* str) {
    char* end;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (!*str) return str;
    end = str + kstrlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end--;
    *(end + 1) = 0;
    return str;
}

static config_entry_t* find_entry(const char* key) {
    if (!key || !*key) return NULL;
    for (u32 i = 0; i < config_count; i++)
        if (kstrcmp(config_entries[i].key, key) == 0) return &config_entries[i];
    return NULL;
}

static config_entry_t* get_or_add(const char* key) {
    if (!key || !*key || kstrlen(key) >= CONFIG_KEY_MAX) return NULL;
    config_entry_t* entry = find_entry(key);
    if (entry) return entry;
    if (config_count >= CONFIG_MAX_ENTRIES) return NULL;
    entry = &config_entries[config_count++];
    kmemset(entry, 0, sizeof(*entry));
    kstrncpy(entry->key, key, CONFIG_KEY_MAX - 1);
    entry->key[CONFIG_KEY_MAX - 1] = 0;
    return entry;
}

static void parse_value(config_entry_t* entry, char* source) {
    if (!entry || !source) return;
    char* value = trim(source);
    if (kstrcmp(value, "true") == 0 || kstrcmp(value, "false") == 0) {
        entry->type = CONFIG_TYPE_BOOL;
        entry->value.boolean = (kstrcmp(value, "true") == 0);
        return;
    }

    bool numeric = *value != 0;
    char* p = value;
    if (*p == '-') p++;
    if (!*p) numeric = false;
    while (*p) {
        if (*p < '0' || *p > '9') { numeric = false; break; }
        p++;
    }
    if (numeric) {
        entry->type = CONFIG_TYPE_INT;
        entry->value.integer = katoi(value, 10);
        return;
    }

    entry->type = CONFIG_TYPE_STRING;
    kstrncpy(entry->value.string, value, CONFIG_VALUE_MAX - 1);
    entry->value.string[CONFIG_VALUE_MAX - 1] = 0;
}

void config_init(void) {
    kmemset(config_entries, 0, sizeof(config_entries));
    config_count = 0;
    vga_puts("Config: Initialized\n");
}

void config_load(const char* path) {
    config_set_string("theme", "charisos_dark");
    config_set_int("volume", 50);
    config_set_bool("boot_sound", true);
    config_set_int("screen_brightness", 100);
    config_set_bool("net_enabled", true);
    config_set_bool("net_dhcp", true);
    config_set_int("font_style", 0);
    config_set_int("font_scale", 1);
    config_set_int("ui_scale", 100);

    vfs_node_t* node = vfs_resolve(path ? path : CONFIG_FILE_PATH);
    if (!node || !(node->flags & VFS_FILE) || !node->read) {
        vga_puts("Config: No settings file found; defaults are active\n");
        return;
    }

    char buffer[4096];
    if (node->size >= sizeof(buffer)) {
        vga_puts("Config: Settings file exceeds the 4095-byte safety limit\n");
        return;
    }
    int length = node->read(node, 0, sizeof(buffer) - 1, (u8*)buffer);
    if (length <= 0 || (u64)length != node->size) {
        vga_puts("Config: Settings file empty or unreadable\n");
        return;
    }
    buffer[length] = 0;

    char* line = buffer;
    while (*line) {
        char* eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        char saved = *eol;
        *eol = 0;
        char* item = trim(line);
        if (*item && *item != '#') {
            char* separator = item;
            while (*separator && *separator != '=') separator++;
            if (*separator == '=') {
                *separator = 0;
                char* key = trim(item);
                char* value = trim(separator + 1);
                config_entry_t* entry = get_or_add(key);
                if (entry) parse_value(entry, value);
            }
        }
        *eol = saved;
        if (*eol == '\r' && *(eol + 1) == '\n') eol++;
        else if (*eol == '\n' && *(eol + 1) == '\r') eol++;
        if (!saved) break;
        line = eol + 1;
    }

    vga_printf("Config: Loaded %u entries from %s\n",
               config_count, path ? path : CONFIG_FILE_PATH);
}

static bool append_bytes(char* output, u32 capacity, u32* length, const char* value) {
    if (!output || !length || !value) return false;
    while (*value) {
        if (*length >= capacity) return false;
        output[(*length)++] = *value++;
    }
    return true;
}

bool config_save(const char* path) {
    const char* target = path ? path : CONFIG_FILE_PATH;
    char output[4096];
    u32 length = 0;
    char digits[32];

    for (u32 i = 0; i < config_count; i++) {
        config_entry_t* entry = &config_entries[i];
        if (!append_bytes(output, sizeof(output), &length, entry->key) ||
            !append_bytes(output, sizeof(output), &length, "=")) return false;

        if (entry->type == CONFIG_TYPE_BOOL) {
            if (!append_bytes(output, sizeof(output), &length,
                              entry->value.boolean ? "true" : "false")) return false;
        } else if (entry->type == CONFIG_TYPE_INT) {
            kitoa(entry->value.integer, digits, 10);
            if (!append_bytes(output, sizeof(output), &length, digits)) return false;
        } else {
            if (!append_bytes(output, sizeof(output), &length, entry->value.string)) return false;
        }
        if (length >= sizeof(output)) return false;
        output[length++] = '\n';
    }

    vfs_node_t* node = vfs_resolve(target);
    if (!node || !(node->flags & VFS_FILE)) {
        /* The current FAT driver supports root-level 8.3 files, not /etc directories. */
        if (fs_create(target) != 0) {
            node = vfs_resolve(target);
            if (!node || !(node->flags & VFS_FILE)) {
                vga_puts("Config: Could not create settings file\n");
                return false;
            }
        }
        node = vfs_resolve(target);
    }
    if (!node || !node->write) {
        vga_puts("Config: Settings file is not writable\n");
        return false;
    }

    /* FAT32 fs_write() extends files but does not shrink them. When a new
     * settings snapshot is shorter than the old one, overwrite the remainder
     * with blank lines so old keys/text cannot survive beyond the new EOF. */
    if (node->size >= sizeof(output)) {
        vga_puts("Config: Existing settings file exceeds the 4096-byte safety limit\n");
        return false;
    }
    while ((u64)length < node->size) output[length++] = '\n';

    int written = node->write(node, 0, length, (const u8*)output);
    if (written != (int)length) {
        vga_puts("Config: Settings write was incomplete\n");
        return false;
    }
    vga_printf("Config: Saved %u bytes to %s\n", length, target);
    return true;
}

int config_get_int(const char* key, int fallback) {
    config_entry_t* entry = find_entry(key);
    return entry && entry->type == CONFIG_TYPE_INT ? entry->value.integer : fallback;
}

const char* config_get_string(const char* key, const char* fallback) {
    config_entry_t* entry = find_entry(key);
    return entry && entry->type == CONFIG_TYPE_STRING ? entry->value.string : fallback;
}

bool config_get_bool(const char* key, bool fallback) {
    config_entry_t* entry = find_entry(key);
    return entry && entry->type == CONFIG_TYPE_BOOL ? entry->value.boolean : fallback;
}

void config_set_int(const char* key, int value) {
    config_entry_t* entry = get_or_add(key);
    if (!entry) return;
    entry->type = CONFIG_TYPE_INT;
    entry->value.integer = value;
}

void config_set_string(const char* key, const char* value) {
    config_entry_t* entry = get_or_add(key);
    if (!entry || !value) return;
    entry->type = CONFIG_TYPE_STRING;
    kstrncpy(entry->value.string, value, CONFIG_VALUE_MAX - 1);
    entry->value.string[CONFIG_VALUE_MAX - 1] = 0;
}

void config_set_bool(const char* key, bool value) {
    config_entry_t* entry = get_or_add(key);
    if (!entry) return;
    entry->type = CONFIG_TYPE_BOOL;
    entry->value.boolean = value;
}
