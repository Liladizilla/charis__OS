/* settings.h - Stable Settings catalog and capability status model.
 *
 * Settings pages describe available OS services; they are not claims that
 * every control on a page has been implemented. A page remains PARTIAL or
 * UNAVAILABLE until its owning subsystem exposes a real, tested backend.
 */
#pragma once

#include <kernel/types.h>

typedef enum {
    SETTINGS_GROUP_PERSONALIZATION = 1,
    SETTINGS_GROUP_DESKTOP_DEVICES = 2,
    SETTINGS_GROUP_APPS_SERVICES = 3,
    SETTINGS_GROUP_SYSTEM = 4,
    SETTINGS_GROUP_LIMIT = 5
} settings_group_t;

typedef enum {
    SETTINGS_PAGE_UNAVAILABLE = 0,
    SETTINGS_PAGE_PARTIAL = 1,
    SETTINGS_PAGE_READY = 2
} settings_page_state_t;

/* Numeric IDs are persistent UI/API identifiers. Do not renumber existing IDs. */
typedef enum {
    SETTINGS_PAGE_INVALID = 0,
    SETTINGS_PAGE_DISPLAY = 1,
    SETTINGS_PAGE_APPEARANCE = 2,
    SETTINGS_PAGE_ACCESSIBILITY = 3,
    SETTINGS_PAGE_WELLBEING = 4,
    SETTINGS_PAGE_MULTITASKING = 5,
    SETTINGS_PAGE_MOUSE_TOUCHPAD = 6,
    SETTINGS_PAGE_KEYBOARD = 7,
    SETTINGS_PAGE_SOUND = 8,
    SETTINGS_PAGE_POWER = 9,
    SETTINGS_PAGE_APPS = 10,
    SETTINGS_PAGE_NOTIFICATIONS = 11,
    SETTINGS_PAGE_SEARCH = 12,
    SETTINGS_PAGE_ONLINE_ACCOUNTS = 13,
    SETTINGS_PAGE_SHARING = 14,
    SETTINGS_PAGE_PRIVACY_SECURITY = 15,
    SETTINGS_PAGE_REGION_LANGUAGE = 16,
    SETTINGS_PAGE_DATE_TIME = 17,
    SETTINGS_PAGE_USERS = 18,
    SETTINGS_PAGE_REMOTE_ACCESS = 19,
    SETTINGS_PAGE_ABOUT_UPDATES = 20,
    SETTINGS_PAGE_ID_LIMIT = 21
} settings_page_id_t;

typedef struct {
    settings_page_id_t id;
    settings_group_t group;
    const char* title;
    const char* summary;
    settings_page_state_t state;
    /* Explains exactly what works and what is still missing. */
    const char* state_detail;
} settings_page_t;

/* The catalog is immutable and allocation-free. */
usize settings_catalog_count(void);
const settings_page_t* settings_catalog_at(usize index);
const settings_page_t* settings_catalog_find(settings_page_id_t id);

/* Returns the number of matching IDs written, capped at capacity.
 * A NULL query, result buffer or zero capacity returns zero. */
usize settings_catalog_search(const char* query,
                              settings_page_id_t* results,
                              usize capacity);

/* Checks IDs, labels, groups, state values and unsupported-state explanations. */
bool settings_catalog_validate(void);

const char* settings_group_label(settings_group_t group);
const char* settings_page_state_label(settings_page_state_t state);
