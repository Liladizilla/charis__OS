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
#include <kernel/vfs.h>
#include <kernel/graphics.h>
#include <kernel/wm.h>
#include <kernel/input.h>
#include <kernel/ipc.h>
#include <kernel/desktop.h>
#include <kernel/apps.h>
#include <kernel/audio.h>
#include <kernel/hda.h>
#include <kernel/gamepad.h>
#include <kernel/pci.h>
#include <kernel/services.h>
#include <kernel/diagnostics.h>
#include <kernel/display.h>
#include <kernel/config.h>
#include <kernel/power.h>
#include <kernel/security.h>
#include <kernel/signal.h>
#include <kernel/pipe.h>
#include <kernel/driver.h>

void kernel_main(u32 magic, u32 info_ptr) {
    // Boot marker helper - writes sequential chars to VGA text buffer at even offsets
    static int boot_marker_col = 0;
    #define BOOT_MARK(c) do { *(u16*)(0xB8000 + (boot_marker_col++ * 2)) = 0x0F00 | (c); } while(0)

    // Debug: kernel entry
    BOOT_MARK('K');

    // Initialize VGA and serial
    vga_init();
    vga_enable_cursor(14, 15);
    BOOT_MARK('V');
    serial_init();
    BOOT_MARK('S');

    // Print boot banner with version and date
    vga_puts_info("=== CharisOS v1.0 ===");
    kprintf("Built: " __DATE__ " " __TIME__ "\n");
    vga_puts_info("Booting...");

    // Validate Multiboot2 magic (0x36d76289 per the Multiboot2 spec)
    if (magic != 0x36d76289) {
        kprintf("[BOOT] Invalid Multiboot2 magic: got 0x%08x expected 0x36d76289\n", magic);
        vga_puts_error("ERROR: Invalid Multiboot2 magic!");
        while (1) {
            asm volatile("hlt");
        }
    }
    BOOT_MARK('M');

    // Initialize subsystems in exact order
    memory_init((void*)(uintptr_t)info_ptr);

    // Optional: VMM tests after VMM init (compile-time flag)
#ifdef RUN_VMM_TESTS
    vmm_run_tests();
#endif

    BOOT_MARK('m');
    idt_init();
    BOOT_MARK('I');
    // Comment out advanced features for boot stability
    irq_init();
    BOOT_MARK('Q');
    timer_init(1000);
    BOOT_MARK('T');
    keyboard_init();
    BOOT_MARK('K');
    ata_init();
    BOOT_MARK('D');  // Disk
    fs_init();
    BOOT_MARK('F');  // Filesystem
    vfs_init();
    BOOT_MARK('v');  // VFS
    // net_init();    // Network support for low-end devices
    // BOOT_MARK('N');
    task_init();
    BOOT_MARK('t');
    scheduler_init();
    BOOT_MARK('S');
    syscall_init();
    BOOT_MARK('C');
    graphics_init();
    BOOT_MARK('G');
    wm_init();
    BOOT_MARK('W');
    input_init();
    BOOT_MARK('i');
    ipc_init();
    BOOT_MARK('p');
    audio_init();
    BOOT_MARK('a');
    signal_init();
    BOOT_MARK('s');
    pipe_init();
    BOOT_MARK('P');
    driver_init();
    BOOT_MARK('d');
    pci_scan();
    BOOT_MARK('P');
    driver_scan_and_bind();
    BOOT_MARK('b');
    config_init();
    BOOT_MARK('c');
    config_load("/etc/charisos.conf");
    BOOT_MARK('L');
    display_init();
    BOOT_MARK('D');
    desktop_init();
    BOOT_MARK('d');
    services_init();
    BOOT_MARK('S');
    diag_init();
    BOOT_MARK('D');
    power_init();
    BOOT_MARK('p');
    security_init();
    BOOT_MARK('S');

    // Initialize network if enabled in config
    BOOT_MARK('n');
    if (config_get_bool("net_enabled", true)) {
        BOOT_MARK('N');
        net_init();
        BOOT_MARK('n');
    }

    // Initialize network if enabled in config
    BOOT_MARK('n');
    if (config_get_bool("net_enabled", true)) {
        BOOT_MARK('N');
        net_init();
        BOOT_MARK('n');
    }
    wm_create_window("CharisOS Desktop", 100, 100, 400, 300);
    apps_init();

    // Create user task with separate address space
    extern void user_main(void);
    pml4_t* user_pml4 = vmm_create_address_space();
    if (user_pml4) {
        vmm_copy_kernel_mappings(user_pml4, NULL);
    }
    task_t* user_task = task_create("user", (task_func_t)user_main, NULL, CAP_SPAWN | CAP_FS_READ, true);
    if (user_task) {
        user_task->mm.pml4 = user_pml4;
        scheduler_add_task(user_task);
    } else {
        vga_puts_error("ERROR: Failed to create user task!");
    }

    // Create shell task with minimal caps
    task_t* shell_task = task_create("shell", (task_func_t)shell_main, NULL, CAP_SPAWN | CAP_FS_READ | CAP_FS_WRITE, false);
    if (shell_task == NULL) {
        vga_puts_error("ERROR: Failed to create shell task!");
        while (1) {
            asm volatile("hlt");
        }
    }
    scheduler_add_task(shell_task);

    vga_puts_success("All systems go. Starting shell.");

    // Boot-completion sentinel.
    // Must go to the serial console (kprintf), not just VGA: CI runs QEMU with
    // -nographic and only captures serial, so this is the line that proves the
    // whole init sequence ran rather than halting partway through.
    kprintf("[BOOT] init complete, entering scheduler\n");

    // Enable interrupts and start scheduler
    asm volatile("sti");
    scheduler_start();

    // Should never reach here - halt loop
    while (1) {
        asm volatile("hlt");
    }
}
