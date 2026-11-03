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
#include <kernel/fs.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/net.h>
#include <kernel/usb.h>
#include <kernel/bootmem.h>
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

#define SETTINGS_TABS 7

static u32 sp(u32 logical) { return gfx_ui_scale(logical); }

static bool opened;
static u32 active_tab;
static u32 active_row;
static bool save_ok;

static const char* const tab_names[SETTINGS_TABS] = {
    "UI", "SYS", "HW", "NET", "DISK", "SEC", "OS"
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

static u32 panel_width(void) {
    u32 available = g_framebuffer.width > 16 ? g_framebuffer.width - 16 : g_framebuffer.width;
    u32 desired = sp(760);
    return desired < available ? desired : available;
}

static u32 settings_text_height(void) {
    return (u32)font_cell_h * gfx_text_get_scale();
}

static u32 title_step(void) {
    u32 base = sp(36), needed = settings_text_height() + sp(8);
    return base > needed ? base : needed;
}

static u32 line_step(void) {
    u32 base = sp(32), needed = settings_text_height();
    return base > needed ? base : needed;
}

static u32 row_height(void) {
    u32 base = sp(52), needed = settings_text_height() + sp(8);
    return base > needed ? base : needed;
}

static u32 row_step(void) {
    u32 base = sp(66), needed = row_height() + sp(12);
    return base > needed ? base : needed;
}

static u32 header_height(void) {
    u32 base = sp(44), needed = settings_text_height() + sp(12);
    return base > needed ? base : needed;
}

static u32 tab_height(void) {
    u32 base = sp(34), needed = settings_text_height() + sp(10);
    return base > needed ? base : needed;
}

static u32 content_top(u32 y) {
    /* Keep the diagnostic rows visible at 3x text on a 768px-tall display. */
    return y + header_height() + sp(4) + tab_height() + sp(6);
}

static u32 footer_height(void) {
    u32 base = sp(48), needed = settings_text_height() + sp(16);
    return base > needed ? base : needed;
}

static u32 panel_height(void) {
    u32 available = g_framebuffer.height > 16 ? g_framebuffer.height - 16 : g_framebuffer.height;
    u32 desired = sp(560);
    u32 needed = content_top(0) + title_step() + line_step() * 10 +
                 footer_height() + sp(4);
    if (needed > desired) desired = needed;
    return desired < available ? desired : available;
}

static u32 panel_x(void) {
    u32 width = panel_width();
    return (g_framebuffer.width - width) / 2;
}

static u32 panel_y(void) {
    u32 height = panel_height();
    return (g_framebuffer.height - height) / 2;
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

static void append_u64(char* out, u32 capacity, u32* pos, u64 value) {
    char digits[21];
    u32 n = 0;
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && n < sizeof(digits));
    while (n && *pos + 1 < capacity) out[(*pos)++] = digits[--n];
    out[*pos] = 0;
}


static void format_pair(char* out, u32 cap, const char* label, u32 value, const char* unit) {
    u32 pos = 0;
    out[0] = 0;
    append_text(out, cap, &pos, label);
    append_u32(out, cap, &pos, value);
    append_text(out, cap, &pos, unit);
}

static void cpuid(u32 leaf, u32 subleaf,
                   u32* eax, u32* ebx, u32* ecx, u32* edx) {
    u32 a = leaf, b = 0, c = subleaf, d = 0;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

static void cpu_vendor(char out[13]) {
    u32 ebx = 0, edx = 0, ecx = 0;
    cpuid(0, 0, NULL, &ebx, &ecx, &edx);
    kmemcpy(out, &ebx, 4);
    kmemcpy(out + 4, &edx, 4);
    kmemcpy(out + 8, &ecx, 4);
    out[12] = 0;
    for (u32 i = 0; i < 12; i++)
        if ((u8)out[i] < 32 || (u8)out[i] > 126) out[i] = '?';
}

static void cpu_brand(char out[49]) {
    u32 maximum = 0;
    cpuid(0x80000000U, 0, &maximum, NULL, NULL, NULL);
    if (maximum >= 0x80000004U) {
        for (u32 leaf = 0; leaf < 3; leaf++) {
            u32 a, b, c, d;
            cpuid(0x80000002U + leaf, 0, &a, &b, &c, &d);
            kmemcpy(out + leaf * 16, &a, 4);
            kmemcpy(out + leaf * 16 + 4, &b, 4);
            kmemcpy(out + leaf * 16 + 8, &c, 4);
            kmemcpy(out + leaf * 16 + 12, &d, 4);
        }
        out[48] = 0;
        for (u32 i = 0; i < 48; i++)
            if ((u8)out[i] < 32 || (u8)out[i] > 126) out[i] = ' ';
        while (out[0] == ' ') kmemmove(out, out + 1, kstrlen(out));
    } else {
        char vendor[13];
        cpu_vendor(vendor);
        kstrncpy(out, vendor, 48);
        out[48] = 0;
    }
}

static void cpu_capabilities(char out[96]) {
    u32 max_basic = 0, eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid(0, 0, &max_basic, NULL, NULL, NULL);
    u32 logical = 1;
    bool apic = false, sse2 = false, nx = false, long_mode = false, syscall = false;
    if (max_basic >= 1) {
        cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        logical = (ebx >> 16) & 0xFFU;
        if (!logical) logical = 1;
        apic = (edx & (1U << 9)) != 0;
        sse2 = (edx & (1U << 26)) != 0;
    }

    u32 max_extended = 0;
    cpuid(0x80000000U, 0, &max_extended, NULL, NULL, NULL);
    if (max_extended >= 0x80000001U) {
        cpuid(0x80000001U, 0, NULL, NULL, NULL, &edx);
        syscall = (edx & (1U << 11)) != 0;
        nx = (edx & (1U << 20)) != 0;
        long_mode = (edx & (1U << 29)) != 0;
    }

    u32 physical_bits = 0;
    if (max_extended >= 0x80000008U) {
        cpuid(0x80000008U, 0, &eax, NULL, NULL, NULL);
        physical_bits = eax & 0xFFU;
    }

    u32 pos = 0;
    out[0] = 0;
    append_text(out, 96, &pos, "Logical CPUs: ");
    append_u32(out, 96, &pos, logical);
    append_text(out, 96, &pos, " | ");
    append_text(out, 96, &pos, apic ? "APIC " : "");
    append_text(out, 96, &pos, sse2 ? "SSE2 " : "");
    append_text(out, 96, &pos, nx ? "NX " : "");
    append_text(out, 96, &pos, long_mode ? "LongMode " : "");
    append_text(out, 96, &pos, syscall ? "SYSCALL" : "NoSYSCALL");
    if (physical_bits) {
        append_text(out, 96, &pos, " | phys ");
        append_u32(out, 96, &pos, physical_bits);
        append_text(out, 96, &pos, "b");
    }
}

static void draw_row(u32 x, u32 y, u32 w, const char* name, const char* value,
                     bool selected) {
    u32 h = row_height();
    fb_fill_rect(x, y, w, h, selected ? S_SELECTED : S_RAISED);
    fb_draw_rect(x, y, w, h, S_BORDER);
    u32 text_y = y + (h > settings_text_height() ?
                      (h - settings_text_height()) / 2 : 0);
    draw_text(x + sp(16), text_y, name, selected ? S_ACCENT : S_TEXT);
    u32 value_width = 0;
    gfx_text_size(value, &value_width, NULL);
    draw_text(x + w - value_width - sp(16), text_y, value, S_MUTED);
}

static void draw_appearance(u32 x, u32 y, u32 w) {
    draw_text(x, y, "Appearance", S_ACCENT);
    y += title_step();
    static const char* const fonts[] = { "Sans", "Monospace", "Serif" };
    static const char* const scales[] = { "80%", "100%", "120%" };
    static const char* const sizes[] = { "1x", "2x", "3x" };
    u32 style = (u32)config_get_int("font_style", 0);
    u32 size = (u32)config_get_int("font_scale", 1);
    u32 ui = (u32)config_get_int("ui_scale", 100);
    if (style > 2) style = 0;
    if (size < 1 || size > 3) size = 1;
    if (ui != 80 && ui != 100 && ui != 120) ui = 100;

    draw_row(x, y, w, "Font family", fonts[style], active_row == 0);
    draw_row(x, y + row_step(), w, "Letter size", sizes[size - 1], active_row == 1);
    draw_row(x, y + row_step() * 2, w, "Interface scale",
             ui == 80 ? scales[0] : ui == 120 ? scales[2] : scales[1], active_row == 2);

    u32 preview_y = y + row_step() * 3 + sp(16);
    draw_text(x, preview_y, "Live typography preview", S_MUTED);
    gfx_draw_string_scaled(x, preview_y + settings_text_height() + sp(8),
                           "CharisOS Settings", S_TEXT, FB_TRANSPARENT, 1);
    u32 note_y = preview_y + settings_text_height() * 2 + sp(20);
    draw_text(x, note_y, "Arrow keys or row clicks change the selected setting.", S_MUTED);
    draw_text(x, note_y + line_step(), "Appearance changes preview immediately; Save keeps them.", S_MUTED);
}

static void draw_system(u32 x, u32 y) {
    draw_text(x, y, "System diagnostics", S_ACCENT);
    y += title_step();
    system_stats_t stats;
    diag_collect_stats(&stats);
    char line[96];
    u32 step = line_step();
    u32 pos;

    format_pair(line, sizeof(line), "Uptime: ", (u32)(stats.uptime_ms / 1000), " seconds");
    draw_text(x, y, line, S_TEXT);
    format_pair(line, sizeof(line), "Tasks active: ", stats.processes_active, "");
    pos = (u32)kstrlen(line);
    append_text(line, sizeof(line), &pos, " / ");
    append_u32(line, sizeof(line), &pos, stats.processes_total);
    append_text(line, sizeof(line), &pos, "   Blocked: ");
    append_u32(line, sizeof(line), &pos, stats.processes_blocked);
    draw_text(x, y + step, line, S_TEXT);
    format_pair(line, sizeof(line), "Allocated pages: ", stats.memory_allocated / 4, "");
    pos = (u32)kstrlen(line); append_text(line, sizeof(line), &pos, "   Reserved: ");
    append_u32(line, sizeof(line), &pos, stats.memory_reserved / 4);
    draw_text(x, y + step * 2, line, S_TEXT);
    format_pair(line, sizeof(line), "Free physical pages: ", stats.memory_free / 4, "");
    draw_text(x, y + step * 3, line, S_TEXT);
    format_pair(line, sizeof(line), "Context switches: ", stats.context_switches, "");
    draw_text(x, y + step * 4, line, S_TEXT);
    format_pair(line, sizeof(line), "Interrupts: ", stats.interrupts, "");
    draw_text(x, y + step * 5, line, S_TEXT);
    format_pair(line, sizeof(line), "System calls: ", stats.syscalls, "");
    draw_text(x, y + step * 6, line, S_TEXT);
    pos = 0; line[0] = 0;
    append_text(line, sizeof(line), &pos, "Firmware-available RAM: ");
    append_u64(line, sizeof(line), &pos, pmm_detected_ram_bytes() / (1024ULL * 1024ULL));
    append_text(line, sizeof(line), &pos, " MiB");
    draw_text(x, y + step * 7, line, S_TEXT);
    draw_text(x, y + step * 8, "The PMM currently manages only the first 1 GiB.", S_MUTED);
    draw_text(x, y + step * 9, "CPU usage remains unavailable until idle-time accounting is verified.", S_MUTED);
}

static void draw_hardware(u32 x, u32 y) {
    draw_text(x, y, "Hardware inventory", S_ACCENT);
    y += title_step();

    char model[49], features[96], line[96];
    u32 step = line_step(), pos = 0;
    cpu_brand(model);
    model[32] = 0;
    cpu_capabilities(features);

    draw_text(x, y, "CPU", S_MUTED);
    draw_text(x + sp(64), y, model, S_TEXT);
    draw_text(x, y + step, features, S_TEXT);

    pos = 0; line[0] = 0;
    append_text(line, sizeof(line), &pos, "Display: ");
    if (g_framebuffer.initialized) {
        append_u32(line, sizeof(line), &pos, g_framebuffer.width);
        append_text(line, sizeof(line), &pos, "x");
        append_u32(line, sizeof(line), &pos, g_framebuffer.height);
        append_text(line, sizeof(line), &pos, " @ ");
        append_u32(line, sizeof(line), &pos, g_framebuffer.bpp);
        append_text(line, sizeof(line), &pos, "bpp");
    } else {
        append_text(line, sizeof(line), &pos, "framebuffer unavailable");
    }
    draw_text(x, y + step * 2, line, S_TEXT);

    format_pair(line, sizeof(line), "PCI devices: ", (u32)pci_count, "");
    draw_text(x, y + step * 3, line, S_TEXT);

    pos = 0; line[0] = 0;
    append_text(line, sizeof(line), &pos, "PS/2 keyboard: ");
    append_text(line, sizeof(line), &pos,
                keyboard_controller_ready() ? "initialized" : "init failed");
    append_text(line, sizeof(line), &pos, " | IRQ1 ");
    append_u32(line, sizeof(line), &pos, keyboard_irq_count());
    draw_text(x, y + step * 4, line, S_TEXT);

    format_pair(line, sizeof(line), "PS/2 mouse packets: ", mouse_packet_count(), "");
    draw_text(x, y + step * 5, line, S_TEXT);
    format_pair(line, sizeof(line), "Input queue dropped: ", input_queue_dropped(), "");
    draw_text(x, y + step * 6, line, S_TEXT);
    format_pair(line, sizeof(line), "USB host controllers: ", (u32)usb_get_controller_count(), "");
    draw_text(x, y + step * 7, line, S_TEXT);
    format_pair(line, sizeof(line), "Boot memory-map regions: ", boot_info.region_count, "");
    draw_text(x, y + step * 8, line, S_TEXT);
    draw_text(x, y + step * 9, "Pending drivers: USB HID, AHCI, NVMe, GPU.", S_MUTED);
}
static void format_mac(char out[18], const u8 mac[6]) {
    static const char hex[] = "0123456789ABCDEF";
    u32 p = 0;
    for (u32 i = 0; i < 6; i++) {
        if (i) out[p++] = ':';
        out[p++] = hex[(mac[i] >> 4) & 0x0F];
        out[p++] = hex[mac[i] & 0x0F];
    }
    out[p] = 0;
}

static void format_ipv4(char out[16], const u8 ip[4]) {
    u32 p = 0;
    for (u32 i = 0; i < 4; i++) {
        if (i) out[p++] = '.';
        append_u32(out, 16, &p, ip[i]);
    }
}

static void draw_network(u32 x, u32 y, u32 w) {
    draw_text(x, y, "Network configuration", S_ACCENT);
    y += title_step();

    bool enabled = config_get_bool("net_enabled", true);
    bool dhcp = config_get_bool("net_dhcp", true);
    draw_row(x, y, w, "Enable networking at startup",
             enabled ? "Enabled" : "Disabled", active_row == 0);
    draw_row(x, y + row_step(), w, "Prefer DHCP configuration",
             dhcp ? "Preferred" : "Static preference", active_row == 1);

    u32 step = line_step();
    u32 info_y = y + row_step() * 2 + sp(12);
    net_interface_t* net = net_get_interface();
    draw_text(x, info_y,
              net && net->initialized ? "RTL8139 Ethernet: initialized" :
                                        "RTL8139 Ethernet: not detected", S_TEXT);

    char mac[18], ip[16], line[96];
    if (net && net->initialized) {
        format_mac(mac, net->mac_addr);
        format_ipv4(ip, net->ip_addr);
    } else {
        kstrncpy(mac, "unavailable", sizeof(mac) - 1);
        mac[sizeof(mac) - 1] = 0;
        kstrncpy(ip, "unavailable", sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = 0;
    }
    draw_text(x, info_y + step, "MAC address", S_MUTED);
    draw_text(x + sp(160), info_y + step, mac, S_TEXT);
    draw_text(x, info_y + step * 2, "Current IPv4", S_MUTED);
    draw_text(x + sp(160), info_y + step * 2, ip, S_TEXT);

    u32 pos = 0; line[0] = 0;
    append_text(line, sizeof(line), &pos, "Packets RX ");
    append_u32(line, sizeof(line), &pos, net ? net->packets_rx : 0);
    append_text(line, sizeof(line), &pos, "   TX ");
    append_u32(line, sizeof(line), &pos, net ? net->packets_tx : 0);
    draw_text(x, info_y + step * 3, line, S_TEXT);

    draw_text(x, info_y + step * 4,
              "DHCP client is not implemented in this build.", S_MUTED);
    draw_text(x, info_y + step * 5,
              "Startup and address preferences require reboot to take effect.", S_MUTED);
    draw_text(x, info_y + step * 6,
              "Driver support is currently limited to RTL8139 polling.", S_MUTED);
}

static void draw_storage(u32 x, u32 y) {
    draw_text(x, y, "Storage and filesystems", S_ACCENT);
    y += title_step();
    u32 step = line_step();

    draw_text(x, y, ata_is_present() ? "ATA device: detected" :
                                      "ATA device: unavailable", S_TEXT);
    draw_text(x, y + step, fs_is_mounted() ? "FAT32 volume: mounted" :
                                             "FAT32 volume: not mounted", S_TEXT);
    draw_text(x, y + step * 2, "Filesystem: root-directory FAT32 support.", S_TEXT);
    draw_text(x, y + step * 3, "Names: current driver supports root-level 8.3 files.", S_MUTED);
    draw_text(x, y + step * 4, "Settings file: /CHARISOS.CFG on writable FAT32.", S_TEXT);
    draw_text(x, y + step * 5, "Executable support: CEX v1 wrapping ELF64 ET_EXEC.", S_TEXT);
    draw_text(x, y + step * 6, "Not implemented: AHCI, NVMe, USB storage, journaling.", S_MUTED);
}

static void draw_security(u32 x, u32 y) {
    draw_text(x, y, "Security status", S_ACCENT);
    y += title_step();
    u32 step = line_step();
    draw_text(x, y, "Kernel stack protector: enabled at compile time", S_TEXT);
    draw_text(x, y + step, "Process isolation: ring-3 address spaces and page permissions", S_TEXT);
    draw_text(x, y + step * 2, "Permissions: task capability bitmasks", S_TEXT);
    draw_text(x, y + step * 3, "Filesystem paths: traversal checks in security layer", S_TEXT);
    draw_text(x, y + step * 4, "Not implemented: Secure Boot and cryptographic random source.", S_MUTED);
    draw_text(x, y + step * 5, "Not implemented: signed packages and full syscall audit.", S_MUTED);
    draw_text(x, y + step * 6, "USB isolation and memory-safe drivers remain future work.", S_MUTED);
    draw_text(x, y + step * 7, "Development build: do not use for sensitive workloads.", S_ACCENT);
}

static void draw_about(u32 x, u32 y) {
    draw_text(x, y, "About CharisOS", S_ACCENT);
    y += title_step();
    u32 step = line_step();
    draw_text(x, y, "CharisOS: native 64-bit x86 operating system.", S_TEXT);
    draw_text(x, y + step, "Kernel and system components are written in C and Assembly.", S_TEXT);
    draw_text(x, y + step * 2, "Native application container: CEX v1.", S_TEXT);
    draw_text(x, y + step * 3, "CEX wraps validated ELF64 ET_EXEC payloads with a checksum.", S_TEXT);
    draw_text(x, y + step * 4, "Checksum detects corruption; it does not authenticate publishers.", S_MUTED);
    draw_text(x, y + step * 5, "BIOS boot path is under active validation; UEFI remains advisory.", S_TEXT);
    draw_text(x, y + step * 6, "Hardware support depends on the drivers present in this build.", S_MUTED);
    draw_text(x, y + step * 7, "Kernel build: " __DATE__ " " __TIME__, S_MUTED);
}

void settings_ui_render(void) {
    if (!opened || !g_framebuffer.initialized) return;
    u32 x = panel_x(), y = panel_y(), w = panel_width(), h = panel_height();
    fb_fill_rect(x + sp(5), y + sp(5), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, S_PANEL);
    fb_draw_rect(x, y, w, h, S_BORDER);
    fb_fill_rect(x, y, w, header_height(), S_RAISED);
    draw_text(x + sp(20), y + (header_height() - settings_text_height()) / 2,
              "System Settings", S_TEXT);

    u32 tab_area = w > sp(32) ? w - sp(32) : 0;
    u32 tab_w = tab_area / SETTINGS_TABS;
    u32 tabs_y = y + header_height() + sp(4);
    u32 tabs_h = tab_height();
    for (u32 i = 0; tab_w && i < SETTINGS_TABS; i++) {
        u32 tx = x + sp(16) + i * tab_w;
        fb_fill_rect(tx, tabs_y, tab_w - sp(4), tabs_h, i == active_tab ? S_SELECTED : S_RAISED);
        fb_draw_rect(tx, tabs_y, tab_w - sp(4), tabs_h, S_BORDER);
        u32 label_y = tabs_y + (tabs_h > settings_text_height() ?
                                (tabs_h - settings_text_height()) / 2 : 0);
        draw_text(tx + sp(8), label_y, tab_names[i], i == active_tab ? S_ACCENT : S_TEXT);
    }

    u32 content_x = x + sp(28);
    u32 content_y = content_top(y);
    u32 content_w = w > sp(56) ? w - sp(56) : 0;
    switch (active_tab) {
    case 0: draw_appearance(content_x, content_y, content_w); break;
    case 1: draw_system(content_x, content_y); break;
    case 2: draw_hardware(content_x, content_y); break;
    case 3: draw_network(content_x, content_y, content_w); break;
    case 4: draw_storage(content_x, content_y); break;
    case 5: draw_security(content_x, content_y); break;
    case 6: draw_about(content_x, content_y); break;
    default: break;
    }

    u32 foot_h = footer_height();
    u32 by = y + h - foot_h;
    u32 button_h = settings_text_height() + sp(8);
    if (button_h > foot_h - sp(8)) button_h = foot_h - sp(8);
    u32 button_y = by + (foot_h - button_h) / 2;
    u32 close_width = 0, save_width = 0;
    gfx_text_size("Close", &close_width, NULL);
    gfx_text_size("Save", &save_width, NULL);
    u32 button_w = close_width > save_width ? close_width : save_width;
    button_w += sp(20);
    if (button_w < sp(88)) button_w = sp(88);
    u32 gap = sp(12);
    u32 buttons_total = button_w * 2 + gap + sp(16);
    u32 save_x = x + (w > buttons_total ? w - buttons_total : 0);
    draw_text(x + sp(20), by + (foot_h > settings_text_height() ?
              (foot_h - settings_text_height()) / 2 : 0),
              save_ok ? "Saved to CHARISOS.CFG" : "Changes are pending until saved", S_MUTED);
    fb_fill_rect(save_x, button_y, button_w, button_h, S_SELECTED);
    fb_draw_rect(save_x, button_y, button_w, button_h, S_BORDER);
    draw_text(save_x + (button_w - save_width) / 2,
              button_y + (button_h - settings_text_height()) / 2, "Save", S_ACCENT);
    fb_fill_rect(save_x + button_w + gap, button_y, button_w, button_h, S_RAISED);
    fb_draw_rect(save_x + button_w + gap, button_y, button_w, button_h, S_BORDER);
    draw_text(save_x + button_w + gap + (button_w - close_width) / 2,
              button_y + (button_h - settings_text_height()) / 2, "Close", S_TEXT);
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
        if (base == KEY_DOWN) {
            u32 max_row = active_tab == 0 ? 2 : active_tab == 3 ? 1 : 0;
            if (active_row < max_row) active_row++;
            return true;
        }
        if (base == KEY_LEFT || base == KEY_RIGHT) {
            int delta = base == KEY_RIGHT ? 1 : -1;
            if (active_tab == 0) adjust_appearance(delta);
            else if (active_tab == 3) {
                config_set_bool(active_row == 0 ? "net_enabled" : "net_dhcp",
                                delta > 0);
                save_ok = false;
            } else active_tab = (active_tab + SETTINGS_TABS + delta) % SETTINGS_TABS;
            return true;
        }
        if (base == KEY_ENTER && active_tab == 0) { adjust_appearance(1); return true; }
        if (base == KEY_ENTER && active_tab == 3) {
            const char* key = active_row == 0 ? "net_enabled" : "net_dhcp";
            config_set_bool(key, !config_get_bool(key, true));
            save_ok = false;
            return true;
        }
        return true;
    }

    if (event->type != INPUT_MOUSE_DOWN) return true;
    int mx = event->mouse.x, my = event->mouse.y;
    u32 x = panel_x(), y = panel_y(), w = panel_width(), h = panel_height();
    if (mx < (int)x || mx >= (int)(x + w) || my < (int)y || my >= (int)(y + h)) {
        settings_ui_close();
        return true;
    }

    u32 tab_area = w > sp(32) ? w - sp(32) : 0;
    u32 tab_w = tab_area / SETTINGS_TABS;
    u32 tabs_y = y + header_height() + sp(4);
    if (tab_w && my >= (int)tabs_y && my < (int)(tabs_y + tab_height()) && mx >= (int)(x + sp(16))) {
        u32 tab = (u32)(mx - (int)x - (int)sp(16)) / tab_w;
        if (tab < SETTINGS_TABS) active_tab = tab;
        return true;
    }

    u32 foot_h = footer_height();
    u32 by = y + h - foot_h;
    u32 button_h = settings_text_height() + sp(8);
    if (button_h > foot_h - sp(8)) button_h = foot_h - sp(8);
    u32 button_y = by + (foot_h - button_h) / 2;
    u32 close_width = 0, save_width = 0;
    gfx_text_size("Close", &close_width, NULL);
    gfx_text_size("Save", &save_width, NULL);
    u32 button_w = close_width > save_width ? close_width : save_width;
    button_w += sp(20);
    if (button_w < sp(88)) button_w = sp(88);
    u32 buttons_total = button_w * 2 + sp(28);
    u32 save_x = x + (w > buttons_total ? w - buttons_total : 0);
    if (my >= (int)button_y && my < (int)(button_y + button_h)) {
        if (mx >= (int)save_x && mx < (int)(save_x + button_w)) {
            save_ok = config_save(CONFIG_FILE_PATH);
            return true;
        }
        if (mx >= (int)(save_x + button_w + sp(12)) && mx < (int)(save_x + button_w * 2 + sp(12))) {
            settings_ui_close();
            return true;
        }
    }

    if ((active_tab == 0 || active_tab == 3) &&
        mx >= (int)(x + sp(28)) && mx < (int)(x + w - sp(28))) {
        u32 count = active_tab == 0 ? 3 : 2;
        u32 content_y = content_top(y) + title_step();
        if (my >= (int)content_y && my < (int)(content_y + row_step() * count)) {
            active_row = (u32)(my - (int)content_y) / row_step();
            if (active_row >= count) active_row = count - 1;
            if (active_tab == 0) {
                int delta = (mx > (int)(x + w - sp(150))) ? 1 : -1;
                adjust_appearance(delta);
            } else {
                const char* key = active_row == 0 ? "net_enabled" : "net_dhcp";
                config_set_bool(key, !config_get_bool(key, true));
                save_ok = false;
            }
            return true;
        }
    }
    return true;
}
