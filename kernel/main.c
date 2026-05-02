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

void kernel_main(u32 magic, u32 info) {
    // Initialize VGA and serial
    vga_init();
    vga_enable_cursor(14, 15);
    serial_init();

    // Print boot banner with version and date
    kprintf("=== CharisOS v1.0 ===\n");
    kprintf("Built: " __DATE__ " " __TIME__ "\n");
    kprintf("Booting...\n");

    // Validate Multiboot2 magic (0x36d76289)
    if (magic != 0x36d76289) {
        kprintf("ERROR: Invalid Multiboot2 magic: 0x%x\n", magic);
        while (1) {
            asm volatile("hlt");
        }
    }

    // Initialize subsystems in exact order
    memory_init(info);
    idt_init();
    irq_init();
    timer_init(1000);
    keyboard_init();
    task_init();
    scheduler_init();
    syscall_init();

    // Create shell task with CAP_ALL
    task_t* shell_task = task_create("shell", shell_main, NULL, CAP_ALL);
    if (shell_task == NULL) {
        kprintf("ERROR: Failed to create shell task!\n");
        while (1) {
            asm volatile("hlt");
        }
    }
    scheduler_add_task(shell_task);

    kprintf("All systems go. Starting shell.\n");

    // Enable interrupts and start scheduler
    asm volatile("sti");
    scheduler_start();

    // Should never reach here - halt loop
    while (1) {
        asm volatile("hlt");
    }
}
