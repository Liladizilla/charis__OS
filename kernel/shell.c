#include <kernel/shell.h>
#include <kernel/vga.h>
#include <kernel/keyboard.h>
#include <kernel/syscall.h>
#include <kernel/string.h>

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
    vga_puts("Shell initialized. Type 'help' for commands.\n");
}

void shell_main(void* arg) {
    char line[128];

    (void)arg; // Unused parameter

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
            vga_puts("Commands:\n");
            vga_puts("  help  - show this help\n");
            vga_puts("  clear - clear screen\n");
            vga_puts("  ls    - list built-in commands\n");
            vga_puts("  echo  - print text\n");
            continue;
        }
        if (cmd_is(line, "ls")) {
            vga_puts("help clear ls echo\n");
            continue;
        }
        if (cmd_is(line, "echo")) {
            const char* text = line + 4;
            while (*text == ' ') text++;
            sys_print(text);
            vga_puts("\n");
            continue;
        }

        vga_puts("Unknown command. Type help.\n");
    }
}
