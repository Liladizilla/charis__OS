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
#include <kernel/logo.h>
#include <kernel/fb.h>
#include <kernel/mouse.h>
#include <kernel/shell_ui.h>

/* The multiboot2 info pointer arrives zero-extended in RDI, so it is a 64-bit
 * value here. Declaring it u32 would silently truncate any address above 4GB. */
void kernel_main(u32 magic, u64 info_ptr) {
    // Boot marker helper - writes sequential chars to VGA text buffer at even offsets
    static int boot_marker_col = 0;
    #define BOOT_MARK(c) do { \
        *(u16*)(uintptr_t)(0xB8000ULL + (u64)(boot_marker_col++) * 2) = 0x0F00 | (u16)(c); \
    } while(0)

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
    memory_init((multiboot_info_t*)(uintptr_t)info_ptr);

    // Set up the linear framebuffer the firmware advertised. Until this runs,
    // every drawing call in the window manager, desktop, widgets and apps is a
    // no-op, because g_framebuffer.initialized is false.
    if (fb_init() == 0) BOOT_MARK('F');

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

    // A linear framebuffer means no hardware text cursor and no hardware mouse
    // pointer, so both are drawn by the shell. Start the mouse here and clamp
    // it to the real display size rather than the placeholder the driver
    // ships with.
    mouse_init();
    if (g_framebuffer.initialized) {
        mouse_set_bounds(g_framebuffer.width, g_framebuffer.height);
    }
    ui_install_input_hooks();

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

    // Ring-3 userspace is not functional yet. Four gaps have to be closed
    // before a task can be switched into ring 3:
    //
    //   1. scheduler_start() never loads CR3. vmm_switch() is only called from
    //      the timer-interrupt path in scheduler_schedule(), so the very first
    //      switch still runs on the kernel's address space.
    //   2. TSS.rsp0 is never set anywhere, so a ring-3 interrupt has no
    //      kernel stack to land on.
    //   3. scheduler.c switches on task->address_space, but this setup path
    //      assigns task->mm.pml4, so the field is left NULL.
    //   4. The task stack comes from kmalloc(), i.e. the kernel heap, and
    //      vmm_copy_kernel_mappings() does not map it into the new PML4, so
    //      the RSP that iretq installs is unmapped.
    //
    // Enqueuing the user task today makes the scheduler switch into ring 3 on
    // the first 1 kHz timer tick and take a triple fault, so the machine
    // reboots instead of reaching a shell. The task is still built, which
    // keeps this setup path compiled and ready, but it is deliberately left
    // out of the ready queue. See TODO.md.
    extern void user_main(void);
    pml4_t* user_pml4 = vmm_create_address_space();
    if (user_pml4) {
        vmm_copy_kernel_mappings(user_pml4, NULL);
    }
    task_t* user_task = task_create("user", (task_func_t)user_main, NULL, CAP_SPAWN | CAP_FS_READ, true);
    if (user_task) {
        user_task->mm.pml4 = user_pml4;
        user_task->address_space = (void*)user_pml4;
        // NOT scheduler_add_task(user_task) -- see above.
    } else {
        vga_puts_error("ERROR: Failed to create user task!");
    }

    // The interactive shell and the desktop both read the keyboard, and
    // keyboard_read_line() drains the driver's ring buffer directly rather
    // than taking events from the input queue. Whichever task runs first
    // takes the key, so with both alive the shell swallows everything and the
    // desktop never sees the Super key or the arrows.
    //
    // So only one of them runs. With a framebuffer the desktop owns the
    // keyboard; without one there is no desktop, and the shell has the
    // keyboard to itself. The serial console still works either way, because
    // kprintf writes to the serial port independently of the task running.
    task_t* shell_task = 0;
    if (!g_framebuffer.initialized) {
        shell_task = task_create("shell", (task_func_t)shell_main, NULL,
                                 CAP_SPAWN | CAP_FS_READ | CAP_FS_WRITE, false);
        if (shell_task == NULL) {
            vga_puts_error("ERROR: Failed to create shell task!");
            while (1) asm volatile("hlt");
        }
        scheduler_add_task(shell_task);
    }

    // The graphical desktop needs a linear framebuffer. Under a BIOS boot the
    // firmware hands over the 80x25 text buffer instead and fb_init() declines,
    // so there is nothing to draw on and the text shell is the whole interface.
    if (g_framebuffer.initialized) {
        task_t* ui_task = task_create("desktop", (task_func_t)shell_ui_main, NULL,
                                      CAP_SPAWN | CAP_FS_READ | CAP_FS_WRITE, false);
        if (ui_task) {
            scheduler_add_task(ui_task);
        } else {
            vga_puts_error("ERROR: Failed to create desktop task!");
        }
    }

    vga_puts_success("All systems go. Starting shell.");

    // Banner goes last, deliberately. BOOT_MARK writes its progress characters
    // to fixed row-0 addresses throughout init, and vga_puts scrolls from the
    // cursor, so printing the banner earlier means the two overwrite each
    // other and neither survives. The same applies to the framebuffer: wm_init
    // and desktop_init blit a cleared backbuffer over the screen, so anything
    // drawn before them is wiped.
    logo_print();
    if (g_framebuffer.initialized) {
        fb_clear(FB_ARGB(0xFF, 0x0D, 0x11, 0x17));
        logo_draw_gfx(300);
    }

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
