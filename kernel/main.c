#include <kernel/types.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/memory.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/syscall.h>
#include <kernel/shell.h>
#include <kernel/printf.h>
#include <kernel/net.h>
#include <kernel/ata.h>
#include <kernel/fs.h>

void kernel_main(u32 magic, u32 info) {
    // Debug: kernel entry
    *(u16*)0xB8000 = 0x0F00 | 'K';

    // Initialize VGA and serial
    vga_init();
    vga_enable_cursor(14, 15);
    *(u16*)0xB8002 = 0x0F00 | 'V';
    serial_init();
    // Debug serial
    asm volatile("mov dx, 0x3F8; mov al, 'K'; out dx, al");
    *(u16*)0xB8004 = 0x0F00 | 'S';

    // Print boot banner with version and date
    vga_puts_info("=== CharisOS v1.0 ===");
    kprintf("Built: " __DATE__ " " __TIME__ "\n");
    vga_puts_info("Booting...");

    // Validate Multiboot2 magic (0x36d76289)
    if (magic != 0x36d76289) {
        vga_puts_error("ERROR: Invalid Multiboot2 magic!");
        while (1) {
            asm volatile("hlt");
        }
    }
    *(u16*)0xB8006 = 0x0F00 | 'M';

    // Initialize subsystems in exact order
    memory_init(info);
    *(u16*)0xB8008 = 0x0F00 | 'm';
    idt_init();
    *(u16*)0xB800A = 0x0F00 | 'I';
    // Comment out advanced features for boot stability
    irq_init();
    *(u16*)0xB800C = 0x0F00 | 'Q';
    timer_init(1000);
    *(u16*)0xB800E = 0x0F00 | 'T';
    keyboard_init();
    *(u16*)0xB8010 = 0x0F00 | 'K';
    ata_init();
    *(u16*)0xB8012 = 0x0F00 | 'D';  // Disk
    fs_init();
    *(u16*)0xB8014 = 0x0F00 | 'F';  // Filesystem
    // net_init();    // Network support for low-end devices
    // *(u16*)0xB8016 = 0x0F00 | 'N';
    task_init();
    *(u16*)0xB8014 = 0x0F00 | 't';
    scheduler_init();
    *(u16*)0xB8016 = 0x0F00 | 'S';
    syscall_init();
    *(u16*)0xB8018 = 0x0F00 | 'C';

    // Create user task with minimal caps
    extern void user_main(void);
    task_t* user_task = task_create("user", user_main, NULL, CAP_SPAWN | CAP_FS_READ, true);
    if (user_task == NULL) {
        vga_puts_error("ERROR: Failed to create user task!");
        while (1) {
            asm volatile("hlt");
        }
    }
    scheduler_add_task(user_task);

    // Create shell task with minimal caps
    task_t* shell_task = task_create("shell", shell_main, NULL, CAP_SPAWN | CAP_FS_READ | CAP_FS_WRITE, false);
    if (shell_task == NULL) {
        vga_puts_error("ERROR: Failed to create shell task!");
        while (1) {
            asm volatile("hlt");
        }
    }
    scheduler_add_task(shell_task);

    vga_puts_success("All systems go. Starting shell.");

    // Enable interrupts and start scheduler
    asm volatile("sti");
    scheduler_start();

    // Should never reach here - halt loop
    while (1) {
        asm volatile("hlt");
    }
}
