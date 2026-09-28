#include <kernel/shell.h>
#include <kernel/vga.h>
#include <kernel/config.h>
#include <kernel/fb.h>
#include <kernel/keyboard.h>
#include <kernel/syscall.h>
#include <kernel/syscall_wrappers.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/scheduler.h>
#include <kernel/audio.h>
#include <kernel/net.h>
#include <kernel/timer.h>
#include <kernel/diagnostics.h>
#include <kernel/services.h>
#include <kernel/power.h>
#include <kernel/security.h>

static void shell_print_prompt(void) {
    vga_puts("charisos> ");
}

static bool cmd_is(const char* line, const char* cmd) {
    usize len = kstrlen(cmd);
    if (kstrncmp(line, cmd, len) == 0) {
        return line[len] == '\0' || line[len] == ' ';
    }
    return false;
}

void shell_init(void) {
    vga_puts_success("Shell initialized. Type 'help' for commands.");
}

bool shell_execute(char* line);

void shell_main(void* arg) {
    char line[128];

    (void)arg; // Unused parameter
    
    vga_clear();
    vga_puts_success("=== CharisOS v1.0 ===");
    vga_puts_info("Low/Medium-end device optimized OS");
    vga_puts("");

    while (1) {
        shell_print_prompt();
        keyboard_read_line(line, sizeof(line));

        if (line[0] == '\0') {
            continue;
        }

        shell_execute(line);
    }
}

/*
 * Run one command. Split out of the shell loop so the graphical start menu can
 * launch the same commands instead of duplicating them.
 */
bool shell_execute(char* line) {
    if (!line || line[0] == '\0') return false;

        if (cmd_is(line, "clear")) {
            vga_clear();
            return true;
        }
        if (cmd_is(line, "help")) {
            vga_puts("Commands:");
            vga_puts("  help    - show this help");
            vga_puts("  clear   - clear screen");
            vga_puts("  ls      - list built-in commands");
            vga_puts("  echo    - print text");
            vga_puts("  net     - network status");
            vga_puts("  uptime  - system uptime");
            return true;
        }
        if (cmd_is(line, "ls")) {
            vga_puts("help clear ls echo net uptime stats tasks services beep sleep shutdown audit");
            vga_puts("terminal files editor settings about");
            return true;
        }
        if (cmd_is(line, "echo")) {
            const char* text = line + 4;
            while (*text == ' ') text++;
            sys_print(text);
            vga_puts("");
            return true;
        }
        if (cmd_is(line, "net")) {
            net_interface_t* ni = net_get_interface();
            if (ni && ni->initialized) {
                kprintf("Network: Initialized (MAC: %02X:%02X:%02X:%02X:%02X:%02X)\n",
                    ni->mac_addr[0], ni->mac_addr[1], ni->mac_addr[2],
                    ni->mac_addr[3], ni->mac_addr[4], ni->mac_addr[5]);
                kprintf("TX: %u packets, RX: %u packets\n", ni->packets_tx, ni->packets_rx);
            } else {
                vga_puts("Network: Not initialized");
            }
            return true;
        }
        if (cmd_is(line, "uptime")) {
            u64 ms = timer_get_ms();
            kprintf("Uptime: %llu seconds\n", ms / 1000);
            return true;
        }
        if (cmd_is(line, "stats")) {
            diag_print_status();
            return true;
        }
        if (cmd_is(line, "tasks")) {
            diag_dump_tasks();
            return true;
        }
        if (cmd_is(line, "services")) {
            service_list();
            return true;
        }
        if (cmd_is(line, "beep")) {
            audio_beep(880, 200);
            return true;
        }
        if (cmd_is(line, "sleep")) {
            power_set_state(POWER_STATE_SLEEP);
            return true;
        }
        if (cmd_is(line, "shutdown")) {
            power_set_state(POWER_STATE_SHUTDOWN);
            while (1) asm volatile("hlt");
        }
        if (cmd_is(line, "audit")) {
            security_audit("manual_audit", scheduler_current());
            return true;
        }

        /* Launched from the graphical start menu as well as typed. */
        if (cmd_is(line, "terminal")) {
            vga_puts_success("Terminal");
            vga_puts("This is the graphical desktop's terminal shortcut.");
            vga_puts("Use the taskbar Start button, or press the Windows key.");
            return true;
        }
        if (cmd_is(line, "files")) {
            vga_puts("File Manager");
            vga_puts("No filesystem is mounted. Attach a FAT32 disk and reboot.");
            return true;
        }
        if (cmd_is(line, "editor")) {
            vga_puts("Text Editor");
            vga_puts("No writable filesystem is mounted yet.");
            return true;
        }
        if (cmd_is(line, "settings")) {
            vga_puts("Settings");
            vga_printf("  theme              = %s\n", config_get_string("theme", "(unset)"));
            vga_printf("  volume             = %d\n", config_get_int("volume", -1));
            vga_printf("  screen_brightness  = %d\n", config_get_int("screen_brightness", -1));
            vga_printf("  boot_sound         = %s\n", config_get_bool("boot_sound", false) ? "true" : "false");
            vga_printf("  net_enabled        = %s\n", config_get_bool("net_enabled", false) ? "true" : "false");
            return true;
        }
        if (cmd_is(line, "about")) {
            vga_puts_success("CharisOS v1.0");
            vga_puts("x86_64 kernel written from scratch in C and NASM.");
            if (g_framebuffer.initialized) {
                vga_printf("Display: %ux%u at %u bpp\n",
                           g_framebuffer.width, g_framebuffer.height, g_framebuffer.bpp);
            } else {
                vga_puts("Display: text console only (no linear framebuffer from firmware)");
            }
            return true;
        }

        vga_puts_error("Unknown command. Type help.");
        return false;
}
