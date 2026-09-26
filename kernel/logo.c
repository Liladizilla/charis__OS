#include <kernel/logo.h>
#include <kernel/vga.h>

/* Printable ASCII keeps the startup identity usable in VGA text mode. */
void logo_print(void) {
    vga_puts("+--------------------------------------+\n");
    vga_puts("|              CHARIS OS               |\n");
    vga_puts("|          x86_64 bare metal           |\n");
    vga_puts("+--------------------------------------+\n\n");
}
