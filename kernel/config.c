#include <kernel/config.h>
#include <kernel/vfs.h>
#include <kernel/string.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

static config_entry_t config_entries[CONFIG_MAX_ENTRIES];
static int config_count = 0;

void config_init(void) {
    config_count = 0;
    vga_puts("Config: Initialized\n");
}

void config_load(const char* path) {
    (void)path;
    // Parse config file - simple key=value format
    // For now, just set defaults
    config_set_string("theme", "charisos_dark");
    config_set_int("volume", 50);
    config_set_bool("boot_sound", true);
    config_set_int("screen_brightness", 100);
}

void config_save(const char* path) {
    (void)path;
    // Write config entries to file
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