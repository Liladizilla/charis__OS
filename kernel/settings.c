#include <kernel/settings.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const settings_page_t settings_pages[] = {
    {
        SETTINGS_PAGE_DISPLAY, SETTINGS_GROUP_PERSONALIZATION,
        "Display",
        "Text size, interface scale, resolution, orientation, refresh rate and monitors.",
        SETTINGS_PAGE_PARTIAL,
        "Font family, text size and interface scale are adjustable. Hardware display modes, orientation, refresh rate and multi-monitor controls are not available."
    },
    {
        SETTINGS_PAGE_APPEARANCE, SETTINGS_GROUP_PERSONALIZATION,
        "Appearance",
        "Themes, accent colors, desktop backgrounds and visual style.",
        SETTINGS_PAGE_PARTIAL,
        "A built-in desktop theme and bundled default wallpaper are active. User-selectable themes, accent colors and a wallpaper gallery are not available yet."
    },
    {
        SETTINGS_PAGE_ACCESSIBILITY, SETTINGS_GROUP_PERSONALIZATION,
        "Accessibility",
        "Text, contrast, magnification, assistive input, captions and screen reading.",
        SETTINGS_PAGE_PARTIAL,
        "Text-size and interface-scale adjustments are available. Contrast modes, screen reading, magnification, captions and assistive input are not yet available."
    },
    {
        SETTINGS_PAGE_WELLBEING, SETTINGS_GROUP_PERSONALIZATION,
        "Wellbeing",
        "Screen time, usage limits, break reminders and focus schedules.",
        SETTINGS_PAGE_UNAVAILABLE,
        "Screen-time accounting, application limits, break reminders and focus scheduling are not implemented."
    },
    {
        SETTINGS_PAGE_MULTITASKING, SETTINGS_GROUP_DESKTOP_DEVICES,
        "Multitasking",
        "Window behavior, application switching, workspaces and screen-edge actions.",
        SETTINGS_PAGE_PARTIAL,
        "Window focus, resize, minimize, maximize and Alt+Tab are available. Virtual workspaces, tiling and configurable edge actions are not implemented."
    },
    {
        SETTINGS_PAGE_MOUSE_TOUCHPAD, SETTINGS_GROUP_DESKTOP_DEVICES,
        "Mouse & Touchpad",
        "Pointer speed, buttons, scrolling, gestures and touchpad behavior.",
        SETTINGS_PAGE_PARTIAL,
        "PS/2 mouse input and pointer bounds are implemented. Per-device sensitivity, button remapping, gesture settings and touchpad configuration are not available."
    },
    {
        SETTINGS_PAGE_KEYBOARD, SETTINGS_GROUP_DESKTOP_DEVICES,
        "Keyboard",
        "Input layouts, language switching, shortcuts and special-character entry.",
        SETTINGS_PAGE_PARTIAL,
        "PS/2 Set-1 keyboard input works with a fixed character mapping. Multiple layouts, configurable shortcuts and compose or special-character entry are not available."
    },
    {
        SETTINGS_PAGE_SOUND, SETTINGS_GROUP_DESKTOP_DEVICES,
        "Sound",
        "Output devices, volume, alerts, application audio and sound controls.",
        SETTINGS_PAGE_PARTIAL,
        "Basic PC-speaker alerts are implemented. General PCM playback, output-device selection, mixing and per-application volume controls are not available."
    },
    {
        SETTINGS_PAGE_POWER, SETTINGS_GROUP_DESKTOP_DEVICES,
        "Power",
        "Power profiles, battery status, idle behavior, suspend and hibernation.",
        SETTINGS_PAGE_PARTIAL,
        "CPU idle uses HLT. Battery telemetry, power profiles, ACPI suspend/resume and hibernation are not implemented."
    },
    {
        SETTINGS_PAGE_APPS, SETTINGS_GROUP_APPS_SERVICES,
        "Apps",
        "Installed applications, default applications and supported file types.",
        SETTINGS_PAGE_PARTIAL,
        "Built-in applications launch from the desktop. An installed-app registry, package manager and default-handler registry are not implemented."
    },
    {
        SETTINGS_PAGE_NOTIFICATIONS, SETTINGS_GROUP_APPS_SERVICES,
        "Notifications",
        "System alerts, per-application notification permissions and quiet modes.",
        SETTINGS_PAGE_UNAVAILABLE,
        "A notification broker, notification history and per-application notification preferences are not implemented."
    },
    {
        SETTINGS_PAGE_SEARCH, SETTINGS_GROUP_APPS_SERVICES,
        "Search",
        "System search, indexed locations and configurable search providers.",
        SETTINGS_PAGE_UNAVAILABLE,
        "There is no indexed system search service or provider registry yet."
    },
    {
        SETTINGS_PAGE_ONLINE_ACCOUNTS, SETTINGS_GROUP_APPS_SERVICES,
        "Online Accounts",
        "Connect external identity providers and manage authorized account access.",
        SETTINGS_PAGE_UNAVAILABLE,
        "OAuth authorization flows, secure token storage and account-provider integrations are not implemented."
    },
    {
        SETTINGS_PAGE_SHARING, SETTINGS_GROUP_APPS_SERVICES,
        "Sharing",
        "File, media and device sharing with explicit access controls.",
        SETTINGS_PAGE_UNAVAILABLE,
        "A sharing service, discovery protocol and user-managed sharing permissions are not implemented."
    },
    {
        SETTINGS_PAGE_PRIVACY_SECURITY, SETTINGS_GROUP_SYSTEM,
        "Privacy & Security",
        "Application permissions, device access, integrity and security controls.",
        SETTINGS_PAGE_PARTIAL,
        "Kernel security primitives exist, but a complete user-facing permission manager, privacy dashboard and enforceable application policy workflow are not available."
    },
    {
        SETTINGS_PAGE_REGION_LANGUAGE, SETTINGS_GROUP_SYSTEM,
        "Region & Language",
        "System language, regional formats, input sources and time-zone conventions.",
        SETTINGS_PAGE_PARTIAL,
        "The current input mapping and clock behavior are fixed. Locale selection, translations, regional formats and time-zone controls are not implemented."
    },
    {
        SETTINGS_PAGE_DATE_TIME, SETTINGS_GROUP_SYSTEM,
        "Date & Time",
        "Clock, calendar, time zone and network time synchronization.",
        SETTINGS_PAGE_PARTIAL,
        "CMOS RTC decoding and monotonic timers exist. Time-zone selection, trusted network time synchronization and a settings UI are not available."
    },
    {
        SETTINGS_PAGE_USERS, SETTINGS_GROUP_SYSTEM,
        "Users",
        "Local accounts, profiles, credentials and session management.",
        SETTINGS_PAGE_UNAVAILABLE,
        "A local multi-user account manager, profile switching and user credential lifecycle are not implemented."
    },
    {
        SETTINGS_PAGE_REMOTE_ACCESS, SETTINGS_GROUP_SYSTEM,
        "Remote Access",
        "Remote desktop and Secure Shell services with auditable authentication.",
        SETTINGS_PAGE_UNAVAILABLE,
        "Remote desktop and SSH services, authentication, network policy and audit support are not implemented."
    },
    {
        SETTINGS_PAGE_ABOUT_UPDATES, SETTINGS_GROUP_SYSTEM,
        "About & Updates",
        "System identity, diagnostics, update channels and recovery.",
        SETTINGS_PAGE_PARTIAL,
        "A native About view exists. Signed update delivery, staged installation and verified rollback are not implemented."
    }
};

static char settings_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
    return value;
}

static bool settings_contains(const char* text, const char* query) {
    if (!text || !query || !*query) return false;

    for (const char* start = text; *start; start++) {
        const char* a = start;
        const char* b = query;
        while (*a && *b &&
               settings_ascii_lower(*a) == settings_ascii_lower(*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

usize settings_catalog_count(void) {
    return (usize)ARRAY_COUNT(settings_pages);
}

const settings_page_t* settings_catalog_at(usize index) {
    if (index >= settings_catalog_count()) return NULL;
    return &settings_pages[index];
}

const settings_page_t* settings_catalog_find(settings_page_id_t id) {
    if (id <= SETTINGS_PAGE_INVALID || id >= SETTINGS_PAGE_ID_LIMIT)
        return NULL;

    for (usize i = 0; i < settings_catalog_count(); i++) {
        if (settings_pages[i].id == id)
            return &settings_pages[i];
    }
    return NULL;
}

usize settings_catalog_search(const char* query,
                              settings_page_id_t* results,
                              usize capacity) {
    if (!query || !*query || !results || !capacity) return 0;

    usize written = 0;
    for (usize i = 0; i < settings_catalog_count() && written < capacity; i++) {
        const settings_page_t* page = &settings_pages[i];
        const char* group = settings_group_label(page->group);
        if (settings_contains(page->title, query) ||
            settings_contains(page->summary, query) ||
            settings_contains(page->state_detail, query) ||
            settings_contains(group, query)) {
            results[written++] = page->id;
        }
    }
    return written;
}

bool settings_catalog_validate(void) {
    for (usize i = 0; i < settings_catalog_count(); i++) {
        const settings_page_t* page = &settings_pages[i];

        if (page->id <= SETTINGS_PAGE_INVALID ||
            page->id >= SETTINGS_PAGE_ID_LIMIT ||
            page->group <= 0 || page->group >= SETTINGS_GROUP_LIMIT ||
            !page->title || !*page->title ||
            !page->summary || !*page->summary ||
            page->state < SETTINGS_PAGE_UNAVAILABLE ||
            page->state > SETTINGS_PAGE_READY)
            return false;

        if (page->state != SETTINGS_PAGE_READY &&
            (!page->state_detail || !*page->state_detail))
            return false;

        for (usize j = i + 1; j < settings_catalog_count(); j++) {
            if (page->id == settings_pages[j].id)
                return false;
        }
    }
    return true;
}

const char* settings_group_label(settings_group_t group) {
    switch (group) {
        case SETTINGS_GROUP_PERSONALIZATION: return "Personalization";
        case SETTINGS_GROUP_DESKTOP_DEVICES: return "Desktop & Devices";
        case SETTINGS_GROUP_APPS_SERVICES: return "Apps & Services";
        case SETTINGS_GROUP_SYSTEM: return "System";
        default: return "Other";
    }
}

const char* settings_page_state_label(settings_page_state_t state) {
    switch (state) {
        case SETTINGS_PAGE_UNAVAILABLE: return "Unavailable";
        case SETTINGS_PAGE_PARTIAL: return "Partially available";
        case SETTINGS_PAGE_READY: return "Available";
        default: return "Unknown";
    }
}
