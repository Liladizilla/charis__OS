#include <kernel/types.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/memory.h>
#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/timer.h>
#include <kernel/rtc.h>
#include <kernel/keyboard.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/syscall.h>
#include <kernel/shell.h>
#include <kernel/printf.h>
#include <kernel/net.h>
#include <kernel/socket.h>
#include <kernel/net_stack.h>
#include <kernel/ata.h>
#include <kernel/fs.h>
#include <kernel/vfs.h>
#include <kernel/graphics.h>
#include <kernel/wm.h>
#include <kernel/input.h>
#include <kernel/ipc.h>
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
#include <kernel/gfx_text.h>
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
#ifdef TEST_KERNEL_STACK_GUARD
static void kernel_stack_guard_probe(void* unused) {
    (void)unused;
    task_t* task = scheduler_current();
    if (!task || !task->guard_page_addr) {
        serial_puts("KERNEL_STACK_GUARD_TEST_SETUP_FAILED\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    serial_puts("KERNEL_STACK_GUARD_TEST_ARMED\n");
    *(volatile u64*)(uintptr_t)task->guard_page_addr =
        0x535441434B475541ULL;

    serial_puts("KERNEL_STACK_GUARD_TEST_FAILED: guard write returned\n");
    for (;;) asm volatile("cli; hlt" : : : "memory");
}
#endif


#ifdef TEST_IPC_WAITERS
static ipc_handle_t ipc_wait_test_sender_handle;
static ipc_handle_t ipc_wait_test_receiver_handle;
static task_t* volatile ipc_wait_test_receiver_task;
static volatile u32 ipc_wait_test_message_received;

static void ipc_wait_test_fail(const char* reason) {
    serial_puts("IPC_WAIT_TEST_FAIL: ");
    serial_puts(reason);
    serial_puts("\n");
    for (;;) asm volatile("cli; hlt" : : : "memory");
}

static void ipc_wait_test_receiver(void* unused) {
    (void)unused;
    static const char expected[] = "charisos-blocking-ipc";
    char buffer[sizeof(expected)];
    u32 sender_pid = 0;

    int result = ipc_recv_wait(ipc_wait_test_receiver_handle,
                               buffer, sizeof(buffer), &sender_pid);
    if (result != (int)sizeof(expected) || sender_pid == 0)
        ipc_wait_test_fail("message wake result or sender identity");
    for (u32 i = 0; i < sizeof(expected); i++) {
        if (buffer[i] != expected[i])
            ipc_wait_test_fail("message payload mismatch");
    }

    ipc_wait_test_message_received = 1;
    /* The sender waits for this second registration before closing its own
     * endpoint, exercising peer-close wake through the real scheduler too. */
    result = ipc_recv_wait(ipc_wait_test_receiver_handle,
                           buffer, sizeof(buffer), &sender_pid);
    if (result != IPC_STATUS_PEER_CLOSED)
        ipc_wait_test_fail("peer-close did not wake blocked receiver");

    serial_puts("IPC_WAIT_TEST_MESSAGE_WAKE_OK\n");
    serial_puts("IPC_WAIT_TEST_PEER_CLOSE_OK\n");
    serial_puts("IPC_WAIT_TEST_OK\n");
}

static void ipc_wait_test_sender(void* unused) {
    (void)unused;
    volatile task_t* receiver = (task_t*)ipc_wait_test_receiver_task;
    if (!receiver) ipc_wait_test_fail("receiver task pointer missing");

    /* Do not send until the receiver has really entered its blocked state.
     * This tests the wakeup path, not the already-queued fast path. */
    u32 attempts = 0;
    while (!receiver->ipc_wait_endpoint && attempts++ < 5000U)
        task_sleep_ms(1);
    if (!receiver->ipc_wait_endpoint ||
        receiver->waiting_event != TASK_WAITING_IPC ||
        receiver->state != TASK_STATE_BLOCKED)
        ipc_wait_test_fail("receiver did not block on empty queue");

    static const char payload[] = "charisos-blocking-ipc";
    if (ipc_send(ipc_wait_test_sender_handle, payload, sizeof(payload)) !=
        IPC_STATUS_OK)
        ipc_wait_test_fail("sender could not publish message");

    attempts = 0;
    while (!ipc_wait_test_message_received && attempts++ < 5000U)
        task_sleep_ms(1);
    if (!ipc_wait_test_message_received)
        ipc_wait_test_fail("receiver did not consume delivered message");

    attempts = 0;
    while (!receiver->ipc_wait_endpoint && attempts++ < 5000U)
        task_sleep_ms(1);
    if (!receiver->ipc_wait_endpoint ||
        receiver->waiting_event != TASK_WAITING_IPC ||
        receiver->state != TASK_STATE_BLOCKED)
        ipc_wait_test_fail("receiver did not re-register for peer close");

    /* Exercise the real task-exit -> ipc_task_close_all -> peer-wakeup path. */
    task_exit_with_status(0);
}

/* Independent workers prove that local endpoint revocation wakes a reader
 * which is genuinely blocked in ipc_recv_wait(). */
static ipc_handle_t ipc_cancel_receiver_handle;
static ipc_handle_t ipc_cancel_peer_handle;
static volatile s32 ipc_cancel_receiver_result;
static volatile s32 ipc_cancel_peer_send_result;
static volatile u32 ipc_cancel_revoke_done;

static void ipc_cancel_receiver(void* unused) {
    (void)unused;
    int result = ipc_recv_wait(ipc_cancel_receiver_handle, NULL, 0, NULL);
    ipc_cancel_receiver_result = result;
    if (result != IPC_STATUS_CANCELLED)
        ipc_wait_test_fail("revoked local endpoint did not cancel blocked receive");
    task_exit_with_status(0);
}

static void ipc_cancel_peer(void* unused) {
    (void)unused;
    u32 attempts = 0;
    while (!ipc_cancel_revoke_done && attempts++ < 5000U)
        task_sleep_ms(1);
    if (!ipc_cancel_revoke_done)
        ipc_wait_test_fail("coordinator did not revoke reader endpoint");
    int result = ipc_send(ipc_cancel_peer_handle, "x", 1);
    ipc_cancel_peer_send_result = result;
    if (result != IPC_STATUS_PEER_CLOSED)
        ipc_wait_test_fail("peer handle survived local endpoint revocation");
    task_exit_with_status(0);
}

static void ipc_cancel_test_coordinator(void* unused) {
    (void)unused;
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");

    task_t* receiver = task_create("ipc-cancel-reader",
        ipc_cancel_receiver, NULL, 0, false);
    task_t* peer = task_create("ipc-cancel-peer",
        ipc_cancel_peer, NULL, 0, false);
    if (!receiver || !peer) {
        asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
        ipc_wait_test_fail("cannot create cancellation workers");
    }

    scheduler_add_task(receiver);
    scheduler_add_task(peer);
    if (!ipc_endpoint_pair_create(receiver, peer,
            IPC_RIGHT_RECEIVE, IPC_RIGHT_SEND,
            &ipc_cancel_receiver_handle, &ipc_cancel_peer_handle)) {
        asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
        ipc_wait_test_fail("cannot create cancellation endpoint pair");
    }
    ipc_cancel_receiver_result = 0x7FFFFFFF;
    ipc_cancel_peer_send_result = 0x7FFFFFFF;
    ipc_cancel_revoke_done = 0;
    asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");

    /* Require the exact BLOCKED state and waiter link before cancellation;
     * a queued-message fast path would not exercise the unlink logic. */
    volatile task_t* blocked_reader = receiver;
    u32 attempts = 0;
    while ((!blocked_reader->ipc_wait_endpoint ||
            blocked_reader->waiting_event != TASK_WAITING_IPC ||
            blocked_reader->state != TASK_STATE_BLOCKED) &&
           attempts++ < 5000U) {
        task_sleep_ms(1);
    }
    if (!blocked_reader->ipc_wait_endpoint ||
        blocked_reader->waiting_event != TASK_WAITING_IPC ||
        blocked_reader->state != TASK_STATE_BLOCKED)
        ipc_wait_test_fail("cancellation reader never blocked");

    ipc_task_close_all(receiver);
    ipc_cancel_revoke_done = 1;

    /* The receiver returns CANCELLED; the delayed peer must see PEER_CLOSED.
     * Reap both children to prove that cancellation leaves no waiter/zombie. */
    for (u32 child = 0; child < 2; child++) {
        u32 status = 0xFFFFFFFFU;
        s64 pid = task_waitpid(-1, &status, false);
        if (pid <= 0 || status != 0)
            ipc_wait_test_fail("cannot reap cancellation worker cleanly");
    }
    if (ipc_cancel_receiver_result != IPC_STATUS_CANCELLED ||
        ipc_cancel_peer_send_result != IPC_STATUS_PEER_CLOSED)
        ipc_wait_test_fail("cancellation or peer invalidation result mismatch");

    serial_puts("IPC_WAIT_TEST_CANCEL_OK\n");
    task_exit_with_status(0);
}
#endif

#ifdef TEST_WAITPID
static volatile u32 waitpid_orphan_pid;

static void waitpid_child_probe(void* requested_status) {
    task_sleep_ms(5);
    task_exit_with_status((u32)(uintptr_t)requested_status);
}

static void waitpid_orphan_child_probe(void* unused) {
    (void)unused;
    task_sleep_ms(15);
    task_exit_with_status(77);
}

static void waitpid_test_fail(const char* reason) {
    serial_puts("WAITPID_TEST_FAIL: ");
    serial_puts(reason);
    serial_puts("\n");
    for (;;) asm volatile("cli; hlt" : : : "memory");
}

static void waitpid_parent_probe(void* unused) {
    (void)unused;
    u32 status = 0xA5A5A5A5U;
    task_t* child = task_create("wait-child", waitpid_child_probe,
                                (void*)(uintptr_t)42, 0, false);
    if (!child) waitpid_test_fail("cannot create exact-PID child");
    u32 child_pid = child->pid;

    s64 result = (s64)syscall_dispatch(SYS_WAITPID, child_pid,
        (u64)(uintptr_t)&status, WAITPID_WNOHANG, 0, 0, 0);
    if (result != 0 || status != 0xA5A5A5A5U)
        waitpid_test_fail("WNOHANG result or status preservation");

    scheduler_add_task(child);
    status = 0;
    result = (s64)syscall_dispatch(SYS_WAITPID, child_pid,
        (u64)(uintptr_t)&status, 0, 0, 0, 0);
    if (result != (s64)child_pid || status != 42 ||
        task_find_by_pid(child_pid) != NULL)
        waitpid_test_fail("exact child wait, exit status or slot reaping");

    if ((s64)syscall_dispatch(SYS_WAITPID, 0, 0, 0, 0, 0, 0) != -1 ||
        (s64)syscall_dispatch(SYS_WAITPID, (u64)(s64)-2, 0, 0, 0, 0, 0) != -1 ||
        (s64)syscall_dispatch(SYS_WAITPID, 0xFFFFFFFEU, 0, 0, 0, 0, 0) != -1)
        waitpid_test_fail("invalid selector or non-child accepted");

    task_t* any_child = task_create("wait-any-child", waitpid_child_probe,
                                    (void*)(uintptr_t)7, 0, false);
    if (!any_child) waitpid_test_fail("cannot create any-child probe");
    u32 any_pid = any_child->pid;
    scheduler_add_task(any_child);
    status = 0;
    result = (s64)syscall_dispatch(SYS_WAITPID, (u64)(s64)-1,
        (u64)(uintptr_t)&status, 0, 0, 0, 0);
    if (result != (s64)any_pid || status != 7 ||
        task_find_by_pid(any_pid) != NULL)
        waitpid_test_fail("wait for any child or status collection");

    task_t* orphan = task_create("wait-orphan", waitpid_orphan_child_probe,
                                 NULL, 0, false);
    if (!orphan) waitpid_test_fail("cannot create orphan probe");
    waitpid_orphan_pid = orphan->pid;
    scheduler_add_task(orphan);
    serial_puts("WAITPID_TEST_OK\n");
}

static void waitpid_orphan_monitor(void* unused) {
    (void)unused;
    for (u32 attempt = 0; attempt < 2000; attempt++) {
        u32 pid = waitpid_orphan_pid;
        if (pid && !task_find_by_pid(pid)) {
            serial_puts("WAITPID_ORPHAN_REAP_OK\n");
            return;
        }
        task_sleep_ms(1);
    }
    serial_puts("WAITPID_ORPHAN_REAP_FAIL\n");
}
#endif

__attribute__((no_stack_protector))
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

    /* Seed stack protection before memory management and device subsystems.
     * kernel_main itself is unprotected so its entry guard remains stable
     * while security_init installs the randomized guard for later functions. */
    serial_puts("[BOOT] security initialization begin");
    security_init();
    BOOT_MARK('S');
    serial_puts("[BOOT] security initialization complete");

    // Initialize subsystems in exact order
    serial_puts("[BOOT] memory initialization begin");
    memory_init((multiboot_info_t*)(uintptr_t)info_ptr);
    serial_puts("[BOOT] memory initialization complete");

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
#ifdef TEST_KERNEL_STACK_GUARD
    /* Exercise a non-error-code vector on IST2. The dedicated #DF gate (8)
     * is wired to the same stack; this probe verifies IST2 frame placement. */
    asm volatile("int $0x81" : : : "memory");
#endif

    /* Prepare the shared event ring before any device driver unmasks an IRQ.
     * Keyboard and mouse handlers can enqueue events as soon as their IRQs
     * are enabled, so initializing the queue later would discard early input. */
    input_init();
    BOOT_MARK('i');

    // Comment out advanced features for boot stability
    irq_init();
    BOOT_MARK('Q');
    timer_init(1000);
    BOOT_MARK('T');
    (void)rtc_init();
    keyboard_init();
    BOOT_MARK('K');
    ata_init();
    BOOT_MARK('D');  // Disk
    fs_init();
    BOOT_MARK('F');  // Filesystem
    vfs_init();
    socket_init();
    BOOT_MARK('v');  // VFS and socket service
#ifdef TEST_FS
    fs_self_test();
#endif
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
    config_load(CONFIG_FILE_PATH);
    gfx_text_apply_preferences();
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

    // Initialize network once, if enabled in config.
    // net_init() resets the RTL8139 and reinitializes its RX/TX state, so
    // calling it twice during boot is unnecessary and can disrupt the device.
    BOOT_MARK('n');
    if (config_get_bool("net_enabled", true)) {
        BOOT_MARK('N');
        net_init();
        if (net_get_interface()->initialized) {
            /* The network worker uses only trusted in-kernel networking APIs.
             * It has no capability-gated filesystem or process syscall need. */
            task_t* net_task = task_create("network", net_poll_task, NULL,
                                           0, false);
            if (net_task) scheduler_add_task(net_task);
            else vga_puts_error("Network: failed to create polling task");
        }
        BOOT_MARK('n');
    }
    /* The graphical shell owns the framebuffer. Do not allocate an empty
     * placeholder WM window that is never painted or integrated with its loop. */
    apps_init();

    // Bootstrap one ring-3 task using the shared low identity map and private
    // page-table levels for its user entry and stack mappings.
    extern void user_main(void);
    pml4_t* user_pml4 = vmm_create_address_space();
    if (user_pml4 && vmm_copy_kernel_mappings(user_pml4, NULL)) {
        /* The normal image deliberately denies exec. A separate CI-only
         * image grants it to prove the authorized ELF handoff end to end. */
        u32 user_capabilities = CAP_SPAWN | CAP_FS_READ;
#ifdef TEST_ELF_EXEC_SUCCESS
        user_capabilities |= CAP_ALLOW_EXEC;
#endif
        task_t* user_task = task_create_with_pml4(
            "user", (task_func_t)user_main, NULL,
            user_capabilities, true, user_pml4);
        if (user_task && task_prepare_user_space(user_task, (void*)user_main)) {
            scheduler_add_task(user_task);
        } else {
            /* A failed bootstrap must not strand its cloned page-table tree.
             * task_prepare_user_space rolls back any user leaf frames first. */
            vmm_destroy_address_space(user_pml4);
            if (user_task) task_discard_unstarted(user_task);
            vga_puts_error("ERROR: Failed to prepare ring-3 task!");
        }
    } else {
        if (user_pml4) vmm_destroy_address_space(user_pml4);
        vga_puts_error("ERROR: Failed to create user address space!");
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
        /* The ring-0 shell uses in-kernel commands and SYS_PRINT only. Keep
         * filesystem and process syscall authority out of its task grant. */
        shell_task = task_create("shell", (task_func_t)shell_main, NULL,
                                 0, false);
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
        /* The ring-0 desktop uses kernel-owned UI and filesystem routines.
         * Its current call graph does not need capability-gated syscalls. */
        task_t* ui_task = task_create("desktop", (task_func_t)shell_ui_main, NULL,
                                      0, false);
        if (ui_task) {
            scheduler_add_task(ui_task);
        } else {
            vga_puts_error("ERROR: Failed to create desktop task!");
        }
    }

#ifdef TEST_KERNEL_STACK_GUARD
    /* Deliberately write into this task's own unmapped kernel-stack guard.
     * The #PF dispatcher must arrive on TSS.IST1 and report containment. */
    task_t* guard_probe = task_create("guard-probe",
        kernel_stack_guard_probe, NULL, 0, false);
    if (!guard_probe) {
        serial_puts("KERNEL_STACK_GUARD_TEST_SETUP_FAILED\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }
    scheduler_add_task(guard_probe);
#endif

#ifdef TEST_IPC_WAITERS
    task_t* ipc_receiver = task_create("ipc-wait-receiver",
        ipc_wait_test_receiver, NULL, 0, false);
    task_t* ipc_sender = task_create("ipc-wait-sender",
        ipc_wait_test_sender, NULL, 0, false);
    if (!ipc_receiver || !ipc_sender) {
        serial_puts("IPC_WAIT_TEST_SETUP_FAILED\n");
    } else {
        ipc_wait_test_receiver_task = ipc_receiver;
        /* Publish both task records before installing owner-bound handles.
         * scheduler_start has not run either test task yet. */
        scheduler_add_task(ipc_receiver);
        scheduler_add_task(ipc_sender);
        if (!ipc_endpoint_pair_create(ipc_receiver, ipc_sender,
                IPC_RIGHT_RECEIVE, IPC_RIGHT_SEND,
                &ipc_wait_test_receiver_handle,
                &ipc_wait_test_sender_handle)) {
            serial_puts("IPC_WAIT_TEST_SETUP_FAILED\n");
        } else {
            task_t* cancel_coordinator = task_create(
                "ipc-cancel-coordinator", ipc_cancel_test_coordinator,
                NULL, 0, false);
            if (!cancel_coordinator) {
                serial_puts("IPC_WAIT_TEST_SETUP_FAILED\n");
            } else {
                scheduler_add_task(cancel_coordinator);
            }
        }
    }
#endif

#ifdef TEST_WAITPID
    task_t* waitpid_monitor = task_create("waitpid-monitor",
        waitpid_orphan_monitor, NULL, 0, false);
    task_t* waitpid_probe = task_create("waitpid-probe",
        waitpid_parent_probe, NULL, 0, false);
    if (!waitpid_monitor || !waitpid_probe) {
        serial_puts("WAITPID_TEST_SETUP_FAILED\n");
    } else {
        scheduler_add_task(waitpid_monitor);
        scheduler_add_task(waitpid_probe);
    }
#endif

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
