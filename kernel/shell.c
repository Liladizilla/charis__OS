#include <kernel/shell.h>
#include <kernel/vga.h>
#include <kernel/keyboard.h>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/printf.h>
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

        if (cmd_is(line, "clear")) {
            vga_clear();
            continue;
        }
        if (cmd_is(line, "help")) {
            vga_puts("Commands:");
            vga_puts("  help    - show this help");
            vga_puts("  clear   - clear screen");
            vga_puts("  ls      - list built-in commands");
            vga_puts("  echo    - print text");
            vga_puts("  net     - network status");
            vga_puts("  uptime  - system uptime");
            continue;
        }
        if (cmd_is(line, "ls")) {
            vga_puts("help clear ls echo net uptime stats tasks services beep sleep shutdown audit");
            continue;
        }
        if (cmd_is(line, "echo")) {
            const char* text = line + 4;
            while (*text == ' ') text++;
            sys_print(text);
            vga_puts("");
            continue;
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
            continue;
        }
        if (cmd_is(line, "uptime")) {
            u64 ms = timer_get_ms();
            kprintf("Uptime: %llu seconds\n", ms / 1000);
            continue;
        }
        if (cmd_is(line, "stats")) {
            diag_print_status();
            continue;
        }
        if (cmd_is(line, "tasks")) {
            diag_dump_tasks();
            continue;
        }
        if (cmd_is(line, "services")) {
            service_list();
            continue;
        }
        if (cmd_is(line, "beep")) {
            audio_beep(880, 200);
            continue;
        }
        if (cmd_is(line, "sleep")) {
            power_set_state(POWER_STATE_SLEEP);
            continue;
        }
        if (cmd_is(line, "shutdown")) {
            power_set_state(POWER_STATE_SHUTDOWN);
            while (1) asm volatile("hlt");
        }
        if (cmd_is(line, "audit")) {
            security_audit("manual_audit", scheduler_current());
            continue;
        }

        vga_puts_error("Unknown command. Type help.");
    }
}
