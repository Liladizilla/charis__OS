/* settings_ui.c - Native system settings, status and diagnostics.
 *
 * This is a kernel-rendered control panel. It deliberately reports only
 * capabilities that current drivers can verify; missing firmware/driver
 * support is displayed as unavailable instead of guessed.
 */
#include <kernel/settings_ui.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/config.h>
#include <kernel/diagnostics.h>
#include <kernel/display.h>
#include <kernel/ata.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <kernel/ui_theme.h>
#include <kernel/security.h>
#include <kernel/string.h>

#define S_BG       CHARIS_UI_BACKGROUND
#define S_PANEL    CHARIS_UI_PANEL
#define S_RAISED   CHARIS_UI_PANEL_RAISED
#define S_BORDER   CHARIS_UI_BORDER
#define S_TEXT     CHARIS_UI_TEXT
#define S_MUTED    CHARIS_UI_MUTED
#define S_ACCENT   CHARIS_UI_ACCENT
#define S_SELECTED CHARIS_UI_ACCENT_SURFACE

#define SETTINGS_TABS 5

static bool opened;
static u32 active_tab;
static u32 active_row;
static bool save_ok;

static const char* const tab_names[SETTINGS_TABS] = {
    "Appearance", "System", "Hardware", "Security", "About"
};

void settings_ui_open(void) {
    opened = true;
    active_tab = 0;
    active_row = 0;
    save_ok = false;
}

void settings_ui_close(void) { opened = false; }
bool settings_ui_is_open(void) { return opened; }

static void draw_text(u32 x, u32 y, const char* value, u32 color) {
    gfx_draw_string(x, y, value, color, FB_TRANSPARENT);
}

static u32 panel_x(void) {
    u32 width = g_framebuffer.width > 760 ? 760 : g_framebuffer.width - 16;
    return (g_framebuffer.width - width) / 2;
}

static u32 panel_y(void) {
    u32 height = g_framebuffer.height > 560 ? 560 : g_framebuffer.height - 16;
    return (g_framebuffer.height - height) / 2;
}

static u32 panel_width(void) {
    return g_framebuffer.width > 760 ? 760 : g_framebuffer.width - 16;
}

static u32 panel_height(void) {
    return g_framebuffer.height > 560 ? 560 : g_framebuffer.height - 16;
}

static void append_u32(char* out, u32 capacity, u32* pos, u32 value) {
    char digits[11];
    u32 n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < sizeof(digits));
    while (n && *pos + 1 < capacity) out[(*pos)++] = digits[--n];
    out[*pos] = 0;
}

static void append_text(char* out, u32 capacity, u32* pos, const char* value) {
    if (!value) return;
    while (*value && *pos + 1 < capacity) out[(*pos)++] = *value++;
    out[*pos] = 0;
}

static void format_pair(char* out, u32 cap, const char* label, u32 value, const char* unit) {
    u32 pos = 0;
    out[0] = 0;
    append_text(out, cap, &pos, label);
    append_u32(out, cap, &pos, value);
    append_text(out, cap, &pos, unit);
}

static void cpu_vendor(char out[13]) {
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    kmemcpy(out, &ebx, 4);
    kmemcpy(out + 4, &edx, 4);
    kmemcpy(out + 8, &ecx, 4);
    out[12] = 0;
    for (u32 i = 0; i < 12; i++) {
        if ((u8)out[i] < 32 || (u8)out[i] > 126) out[i] = '?';
    }
}

static void draw_row(u32 x, u32 y, u32 w, const char* name, const char* value,
                     bool selected) {
    fb_fill_rect(x, y, w, 52, selected ? S_SELECTED : S_RAISED);
    fb_draw_rect(x, y, w, 52, S_BORDER);
    draw_text(x + 16, y + 7, name, selected ? S_ACCENT : S_TEXT);
    draw_text(x + 16, y + 29, value, S_MUTED);
}

static void draw_appearance(u32 x, u32 y, u32 w) {
    static const char* const fonts[] = { "Bitmap", "Bold bitmap", "Compact bitmap" };
    static const char* const scales[] = { "80%", "100%", "120%" };
    static const char* const sizes[] = { "1x", "2x", "3x" };
    u32 style = (u32)config_get_int("font_style", 0);
    u32 size = (u32)config_get_int("font_scale", 1);
    u32 ui = (u32)config_get_int("ui_scale", 100);
    if (style > 2) style = 0;
    if (size < 1 || size > 3) size = 1;
    if (ui != 80 && ui != 100 && ui != 120) ui = 100;

    draw_row(x, y, w, "Font family", fonts[style], active_row == 0);
    draw_row(x, y + 66, w, "Letter size", sizes[size - 1], active_row == 1);
    draw_row(x, y + 132, w, "Interface scale", ui == 80 ? scales[0] : ui == 120 ? scales[2] : scales[1],
             active_row == 2);

    u32 old_style = style, old_size = size;
    config_set_int("font_style", (int)style);
    config_set_int("font_scale", (int)size);
    gfx_text_apply_preferences();
    draw_text(x, y + 222, "Live typography preview", S_MUTED);
    gfx_draw_string_scaled(x, y + 250, "CharisOS Settings", S_TEXT, FB_TRANSPARENT, 1);
    config_set_int("font_style", (int)old_style);
    config_set_int("font_scale", (int)old_size);
    gfx_text_apply_preferences();
    draw_text(x, y + 300, "Arrow keys or row clicks change the selected setting.", S_MUTED);
    draw_text(x, y + 324, "Appearance changes preview immediately; Save keeps them.", S_MUTED);
}

static void draw_system(u32 x, u32 y) {
    system_stats_t stats;
    diag_collect_stats(&stats);
    char line[96];
    u32 pos = 0;

    format_pair(line, sizeof(line), "Uptime: ", (u32)(stats.uptime_ms / 1000), " seconds");
    draw_text(x, y, line, S_TEXT);
    format_pair(line, sizeof(line), "Tasks active: ", stats.processes_active, "");
    pos = (u32)kstrlen(line); append_text(line, sizeof(line), &pos, " / ");
    append_u32(line, sizeof(line), stats.processes_total);
    draw_text(x, y + 32, line, S_TEXT);
    format_pair(line, sizeof(line), "Tasks blocked: ", stats.processes_blocked, "");
    draw_text(x, y + 52, line, S_TEXT);
    format_pair(line, sizeof(line), "Physical pages used: ", stats.memory_used / 4, "");
    draw_text(x, y + 64, line, S_TEXT);
    format_pair(line, sizeof(line), "Physical pages free: ", stats.memory_free / 4, "");
    draw_text(x, y + 96, line, S_TEXT);
    format_pair(line, sizeof(line), "Context switches: ", stats.context_switches, "");
    draw_text(x, y + 128, line, S_TEXT);
    format_pair(line, sizeof(line), "Interrupts: ", stats.interrupts, "");
    draw_text(x, y + 160, line, S_TEXT);
    format_pair(line, sizeof(line), "System calls: ", stats.syscalls, "");
    draw_text(x, y + 192, line, S_TEXT);
    draw_text(x, y + 244, "Memory figures cover the currently managed 1 GiB physical window.", S_MUTED);
    draw_text(x, y + 268, "CPU utilization is not reported until idle accounting is trustworthy.", S_MUTED);
}

static void draw_hardware(u32 x, u32 y) {
    char vendor[13];
    char line[96];
    cpu_vendor(vendor);
    draw_text(x, y, "Processor vendor", S_MUTED);
    draw_text(x + 220, y, vendor, S_TEXT);

    if (g_framebuffer.initialized) {
        u32 pos = 0;
        line[0] = 0;
        append_u32(line, sizeof(line), &pos, g_framebuffer.width);
        append_text(line, sizeof(line), &pos, " x ");
        append_u32(line, sizeof(line), &pos, g_framebuffer.height);
        append_text(line, sizeof(line), &pos, " @ ");
        append_u32(line, sizeof(line), &pos, g_framebuffer.bpp);
        append_text(line, sizeof(line), &pos, " bpp");
    } else {
        kstrncpy(line, "Framebuffer unavailable", sizeof(line) - 1);
        line[sizeof(line) - 1] = 0;
    }
    draw_text(x, y + 44, "Display mode", S_MUTED);
    draw_text(x + 220, y + 44, line, S_TEXT);

    format_pair(line, sizeof(line), "PCI devices enumerated: ", (u32)pci_count, "");
    draw_text(x, y + 88, line, S_TEXT);
    draw_text(x, y + 132, ata_is_present() ? "Primary ATA device: detected" :
                                             "Primary ATA device: unavailable", S_TEXT);
    draw_text(x, y + 188, "Current storage driver: legacy ATA PIO only.", S_MUTED);
    draw_text(x, y + 212, "SATA/AHCI, NVMe, USB HID and broad GPU support are not yet complete.", S_MUTED);
}

static void draw_security(u32 x, u32 y) {
    draw_text(x, y, "Kernel stack protector: enabled at compile time", S_TEXT);
    draw_text(x, y + 36, "Process isolation: ring-3 address spaces and page permissions", S_TEXT);
    draw_text(x, y + 72, "Permissions: task capability bitmasks", S_TEXT);
    draw_text(x, y + 108, "Filesystem paths: traversal checks in security layer", S_TEXT);
    draw_text(x, y + 160, "Not yet implemented or verified:", S_ACCENT);
    draw_text(x, y + 192, "Secure Boot chain, cryptographic random source, signed packages,", S_MUTED);
    draw_text(x, y + 216, "full syscall audit coverage, memory-safe drivers and USB isolation.", S_MUTED);
    draw_text(x, y + 272, "Do not treat this development build as safe for sensitive workloads.", S_ACCENT);
}

static void draw_about(u32 x, u32 y) {
    draw_text(x, y, "CharisOS", S_ACCENT);
    draw_text(x, y + 36, "64-bit x86 operating system written in C and Assembly.", S_TEXT);
    draw_text(x, y + 72, "Current executable support: validated ELF64 ET_EXEC loading.", S_TEXT);
    draw_text(x, y + 108, "CEX v1 wraps ELF64 with architecture metadata and a checksum.", S_MUTED);
    draw_text(x, y + 160, "Build target: freestanding x86_64, BIOS boot path under active testing.", S_TEXT);
    draw_text(x, y + 196, "Hardware support is driver-specific and is not universal yet.", S_MUTED);
}

void settings_ui_render(void) {
    if (!opened || !g_framebuffer.initialized) return;
    u32 x = panel_x(), y = panel_y(), w = panel_width(), h = panel_height();
    fb_fill_rect(x + 5, y + 5, w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, S_PANEL);
    fb_draw_rect(x, y, w, h, S_BORDER);
    fb_fill_rect(x, y, w, 44, S_RAISED);
    draw_text(x + 20, y + 14, "System Settings", S_TEXT);

    u32 tab_w = (w - 32) / SETTINGS_TABS;
    for (u32 i = 0; i < SETTINGS_TABS; i++) {
        u32 tx = x + 16 + i * tab_w;
        fb_fill_rect(tx, y + 56, tab_w - 4, 34, i == active_tab ? S_SELECTED : S_RAISED);
        fb_draw_rect(tx, y + 56, tab_w - 4, 34, S_BORDER);
        draw_text(tx + 8, y + 67, tab_names[i], i == active_tab ? S_ACCENT : S_TEXT);
    }

    u32 content_x = x + 28;
    u32 content_y = y + 112;
    u32 content_w = w - 56;
    switch (active_tab) {
    case 0: draw_appearance(content_x, content_y, content_w); break;
    case 1: draw_system(content_x, content_y); break;
    case 2: draw_hardware(content_x, content_y); break;
    case 3: draw_security(content_x, content_y); break;
    case 4: draw_about(content_x, content_y); break;
    default: break;
    }

    u32 by = y + h - 48;
    draw_text(x + 20, by + 12, save_ok ? "Saved to CHARISOS.CFG" : "Changes are pending until saved", S_MUTED);
    u32 save_x = x + w - 206;
    fb_fill_rect(save_x, by + 4, 88, 32, S_SELECTED);
    fb_draw_rect(save_x, by + 4, 88, 32, S_BORDER);
    draw_text(save_x + 24, by + 15, "Save", S_ACCENT);
    fb_fill_rect(save_x + 100, by + 4, 88, 32, S_RAISED);
    fb_draw_rect(save_x + 100, by + 4, 88, 32, S_BORDER);
    draw_text(save_x + 120, by + 15, "Close", S_TEXT);
}

static void adjust_appearance(int direction) {
    int style = config_get_int("font_style", 0);
    int size = config_get_int("font_scale", 1);
    int scale = config_get_int("ui_scale", 100);
    if (active_row == 0) {
        style = (style + 3 + direction) % 3;
        config_set_int("font_style", style);
    } else if (active_row == 1) {
        size += direction;
        if (size < 1) size = 3;
        if (size > 3) size = 1;
        config_set_int("font_scale", size);
    } else {
        scale += direction * 20;
        if (scale < 80) scale = 120;
        if (scale > 120) scale = 80;
        config_set_int("ui_scale", scale);
    }
    gfx_text_apply_preferences();
    save_ok = false;
}

bool settings_ui_handle_event(const input_event_t* event) {
    if (!opened || !event) return false;

    if (event->type == INPUT_KEY_DOWN) {
        u8 code = (u8)event->key.scancode;
        u8 base = code & 0x7F;
        if (base == KEY_ESCAPE) { settings_ui_close(); return true; }
        if (base == KEY_UP) { if (active_row) active_row--; return true; }
        if (base == KEY_DOWN) { if (active_row < 2) active_row++; return true; }
        if (base == KEY_LEFT || base == KEY_RIGHT) {
            int delta = base == KEY_RIGHT ? 1 : -1;
            if (active_tab == 0) adjust_appearance(delta);
            else active_tab = (active_tab + SETTINGS_TABS + delta) % SETTINGS_TABS;
            return true;
        }
        if (base == KEY_ENTER && active_tab == 0) { adjust_appearance(1); return true; }
        return true;
    }

    if (event->type != INPUT_MOUSE_DOWN) return true;
    int mx = event->mouse.x, my = event->mouse.y;
    u32 x = panel_x(), y = panel_y(), w = panel_width(), h = panel_height();
    if (mx < (int)x || mx >= (int)(x + w) || my < (int)y || my >= (int)(y + h)) {
        settings_ui_close();
        return true;
    }

    u32 tab_w = (w - 32) / SETTINGS_TABS;
    if (my >= (int)(y + 56) && my < (int)(y + 90) && mx >= (int)(x + 16)) {
        u32 tab = (u32)(mx - (int)x - 16) / tab_w;
        if (tab < SETTINGS_TABS) active_tab = tab;
        return true;
    }

    u32 by = y + h - 48;
    u32 save_x = x + w - 206;
    if (my >= (int)(by + 4) && my < (int)(by + 36)) {
        if (mx >= (int)save_x && mx < (int)(save_x + 88)) {
            save_ok = config_save(CONFIG_FILE_PATH);
            return true;
        }
        if (mx >= (int)(save_x + 100) && mx < (int)(save_x + 188)) {
            settings_ui_close();
            return true;
        }
    }

    if (active_tab == 0 && mx >= (int)(x + 28) && mx < (int)(x + w - 28)) {
        u32 content_y = y + 112;
        if (my >= (int)content_y && my < (int)(content_y + 184)) {
            active_row = (u32)(my - (int)content_y) / 66;
            if (active_row > 2) active_row = 2;
            int delta = (mx > (int)(x + w - 150)) ? 1 : -1;
            adjust_appearance(delta);
            return true;
        }
    }
    return true;
}
