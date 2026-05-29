/* config.h - System configuration for CharisOS */
#pragma once
#include <kernel/types.h>

#define CONFIG_MAX_ENTRIES 64
#define CONFIG_KEY_MAX     32
#define CONFIG_VALUE_MAX   256

typedef enum {
    CONFIG_TYPE_INT,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_BOOL
} config_type_t;

typedef struct {
    char key[CONFIG_KEY_MAX];
    char value[CONFIG_VALUE_MAX];
    config_type_t type;
} config_entry_t;

void config_init(void);
void config_load(const char* path);
void config_save(const char* path);

int config_get_int(const char* key, int default_value);
const char* config_get_string(const char* key, const char* default_value);
bool config_get_bool(const char* key, bool default_value);

void config_set_int(const char* key, int value);
void config_set_string(const char* key, const char* value);
void config_set_bool(const char* key, bool value);