# TODO - CharisOS Development Roadmap

## Phase 1: Memory/VMM Refactor (COMPLETE)
- [x] heap.c - separate heap module with kmalloc/kfree
- [x] bootmem.c - multiboot parsing skeleton
- [x] memory.c - thin orchestrator
- [x] vmm_walk(), vmm_create_address_space(), vmm_copy_kernel_mappings(), vmm_switch()
- [x] vmm_map_page_pml4() - map into specific PML4
- [x] PMM safety: double-free check, next-fit cursor

## Phase 2: Syscall Interface & User Space (COMPLETE)
- [x] SYSCALL/SYSRET assembly entry and MSR setup (user bootstrap wrappers currently use DPL3 `int 0x80`)
- [x] sys_exit(), sys_getpid(), sys_yield(), sys_sleep()
- [x] sys_read()/sys_write() with fd-based I/O
- [x] sys_print() - debug output
- [x] sys_fork() - creates child with separate PML4
- [x] task_create_with_pml4() - create task with custom address space
- [x] Per-process fd table (stdin/stdout/stderr pre-opened)

## Phase 3: Filesystem (COMPLETE)
- [x] VFS layer (vfs.h, vfs.c) - vfs_node_t, mount points
- [x] Device nodes: /dev/null, /dev/zero, /dev/kbd, /dev/vga
- [x] Per-task fd table with fd_alloc(), fd_get(), fd_close()
- [x] FAT32 integration with VFS (read-only)
- [x] sys_open() / sys_close() syscalls

## Phase 4: ELF Loader (COMPLETE)
- [x] elf.h - ELF header/structures
- [x] elf.c - ELF parsing (ehdr, phdr), PT_LOAD mapping
- [x] sys_exec() syscall - load and run ELF program

## Phase 5: Graphics & Display (COMPLETE)
- [x] fb.h/fb.c - framebuffer driver (VESA/GOP ready)
- [x] psf.h/psf.c - PC Screen Font renderer (PSF2 support)
- [x] fb_put_pixel(), fb_clear(), fb_fill_rect()
- [x] graphics.h/graphics.c - 2D drawing primitives
- [x] graphics_line() - Bresenham's algorithm
- [x] graphics_rect() - filled/outline
- [x] graphics_circle() - midpoint circle
- [x] graphics_put_string() - PSF-based text rendering

## Phase 6: Window System (MOSTLY COMPLETE)
- [x] Window manager (compositor.c, wm.c)
- [x] Widget library (widgets.c)
- [x] Input event system (input.c, keyboard.c, mouse.c)
- [x] PS/2 mouse driver
- [ ] Window compositing effects (frosted/acrylic materials in compositor.h are stubs)
- [ ] Window manager needs stress testing (32 task limit)

## Phase 7: IPC & Signals (PARTIAL)
- [x] Signals (SIGINT, SIGSEGV, SIGCHLD) - signal.c exists
- [x] Pipes - pipe.c implemented
- [x] Shared memory (shm.c)
- [ ] Unix domain sockets (socket.c is INET-only)
- [ ] wait()/waitpid() syscall missing (no zombie reaping)

## Phase 8: Network Stack (PARTIAL - ENABLED BUT INCOMPLETE)
- [x] Socket API (AF_INET, SOCK_STREAM/SOCK_DGRAM) - socket.c
- [x] Socket syscalls (SYS_SOCKET, SYS_CONNECT, SYS_BIND, SYS_LISTEN, SYS_ACCEPT, SYS_SEND, SYS_RECV, SYS_SOCKET_CLOSE)
- [x] RTL8139 driver (net.c) - finds device via PCI, basic TX implemented
- [ ] TCP/IP stack foundation (net_handle_tcpip is stub)
- [ ] ARP/ICMP implementation
- [ ] DHCP/static IP config via config.c
- [ ] RX packet handling (net_recv_packet returns 0)
- [ ] IRQ-driven network processing

## Phase 9: Desktop Environment (PARTIAL)
- [x] Desktop with icons and taskbar (desktop.c)
- [x] Window decorations (titlebar, borders, drag/move)
- [ ] Settings app does not actually persist settings (wires to config.c but no UI)
- [ ] File Manager app - UI only, no VFS operations wired
- [ ] Text Editor app - UI only
- [ ] Calculator app - UI only

## Phase 10: User Applications (UI STUBS ONLY)
- [ ] Built-in apps (Terminal, File Manager, Text Editor, Calculator, Settings) - all create windows but have no functional implementation
- [ ] Game SDK with graphics/audio syscalls - headers exist, syscalls registered, but no actual games

## Phase 11: Hardware Abstraction (PARTIAL)
- [x] HDA audio driver with PCI binding (hda.c) - probe logic fixed
- [x] USB device enumeration placeholder (usb.c - stub)
- [x] Driver framework with probe/remove callbacks (driver.c) - wildcard match bug fixed
- [x] PCI class/subclass detection in pci_scan()
- [ ] ATA driver (ata.c) - basic read only, no write, no identify
- [ ] Second ATA driver (disk.c) - REMOVED (dead code)
- [ ] GPU/framebuffer driver for real hardware (fb.c uses fixed 640x480)

## Phase 12: Graphics Acceleration (PARTIAL)
- [x] Software rasterizer (16.16 fixed-point) - raster.c
- [x] Triangle rasterization with depth buffer
- [x] Barycentric coordinate interpolation
- [ ] Integration with window system (not wired)

## Phase 13: Gaming Support (HEADERS ONLY)
- [x] Gamepad subsystem (gamepad.c - stub)
- [x] Game SDK header (sdk/charis_game.h)
- [x] SYS_GAME_* syscalls for graphics/audio - registered but minimal impl
- [ ] Actual game demo

## Phase 14: Security & Capabilities (PARTIAL - NOW ENFORCED)
- [x] Capability bitmask per task (CAP_FS_READ, CAP_FS_WRITE, CAP_SPAWN, etc.)
- [x] Capability inheritance on fork
- [x] Capability enforcement in sys_open() and sys_exec() (NEW)
- [x] Path traversal blocking in security_verify_path() (NEW)
- [ ] Capability enforcement in VFS write/read paths
- [ ] Capability enforcement in socket/net syscalls
- [ ] Real entropy for security_generate_token() (currently predictable XOR)
- [ ] Persistent audit log

## Phase 15: Configuration & Persistence (PARTIAL - NOW WORKING)
- [x] In-memory config store (config.c) - typed get/set
- [x] config_load() - parses /etc/charisos.conf via VFS (NEW)
- [x] config_save() - writes to /etc/charisos.conf via VFS (NEW)
- [x] Default settings: theme, volume, boot_sound, brightness, net_enabled
- [ ] Settings UI that calls config_save()
- [ ] Config file creation if missing

## Phase 16: Build & Quality (IN PROGRESS)
- [x] Kernel compiles cleanly (all 7 errors fixed)
- [x] CI workflow (.github/workflows/ci.yml) - build + QEMU ring-3 yield/resume boot test
- [x] Repo hygiene: .gitattributes, .gitignore, CRLF→LF fixed
- [x] Boot debug markers fixed (no collisions, full coverage)
- [ ] vmm_test.c wired into make test (compiles, but RUN_VMM_TESTS only for vmm_test.o)
- [ ] Dead code removed (disk.c deleted)
- [ ] Add -Werror to Makefile
- [ ] Automated regression test suite

## Phase 17: Process Management (IN PROGRESS)
- [x] Bootstrap user task enters ring 3 with a private address-space branch and mapped user stack
- [x] DPL3 syscall, yield to a kernel task, and resume the user interrupt frame (QEMU verified)
- [ ] Load ELF programs into runnable user tasks
- [ ] Complete user ABI/libc and validate SYSRET path
- [ ] Per-task kernel interrupt stacks and SMP-safe TSS handling
- [ ] wait()/waitpid() syscall for zombie reaping
- [ ] Process groups/sessions
- [ ] Signal delivery to process groups
- [ ] Resource limits (RLIMIT_*)

---

## Summary of Audit Fixes Applied (Sep 2026)

| Priority | Issue | Status |
|----------|-------|--------|
| 1 | 7 compilation errors | ✅ FIXED |
| 2 | CI workflow | ✅ ADDED (now greps a post-init sentinel) |
| 3 | Repo hygiene (node_modules, CRLF, logs) | ✅ FIXED |
| 4 | Boot debug markers (collisions, gaps) | ✅ FIXED |
| 5 | Dead code (disk.c, vmm_test.c) | ✅ FIXED (disk.c removed, vmm_test.c wired) |
| 6 | Config persistence | ✅ IMPLEMENTED |
| 7 | Capability enforcement + PCI wildcard | ✅ IMPLEMENTED |
| 8 | Networking (net_init, socket_send) | ✅ ENABLED (RTL8139 TX wired, RX/TCP stubs) |
| 9 | TODO.md sync | ✅ THIS FILE |

---

## Boot-blocking bugs found during the Sep 2026 audit

These were not in the original audit. They were found by instrumenting
`kernel_main()` with serial traces and stepping through a real QEMU boot. The
first three each caused the kernel to hang, which meant **every subsystem
initialised after the hang point had never actually executed** — so several
"verified" items above were only verified to compile, not to run.

| Bug | Location | Effect | Fix |
|---|---|---|---|
| `u8 bus` compared `< 256` | `kernel/pci.c:12` | `pci_scan()` looped forever. GCC emitted `-Wtype-limits` and the warning was ignored. Everything after `pci_scan()` in `kernel_main()` — config, desktop, services, diagnostics, power, security, net_init, task creation, `scheduler_start()` — never ran. | Widen the loop variable to `u16`. |
| FAT32 walk with no volume | `kernel/fs.c` | `fs_init()` bails with "Not FAT32" when no disk is attached, but set no validity flag. `fs_open()` then walked a garbage FAT chain from an uninitialised boot sector and hung inside `config_load()`. This is the common case: booting from CD-ROM with no hard disk. | Added an `fs_mounted` flag plus geometry validation (non-zero `sectors_per_cluster`/`num_fats`/`fat_size_32`, sane `data_start` and `root_cluster`); `fs_open`/`fs_read` now return `-1` immediately when unmounted. |
| Multiboot2 magic clobbered | `boot/boot.asm` | The debug output at the top of `start:` wrote to `AL` and `AH` before `EAX` was saved, so `mb_magic` stored corrupted data. The kernel then rejected its own boot. | Save `EAX`/`EBX` as the first two instructions of `start:`, before any debug output. |
| `char c` compared `> 127` | `kernel/psf.c:30,62` | `char` is signed on x86, so `c > 127` is always false. Latin-1/high-byte characters are never skipped and index out of the intended glyph range. | Cast to `unsigned char`, or compare `(u8)c > 127`. *(not yet fixed)* |
| Wrong initial return address | `kernel/task.c` | `task_create()` pushed `task_exit_handler` where `context_switch`'s `ret` lands. The task's entry function was never called; `task_exit_handler` ran instead and `kfree`'d the stack it was executing on. Triple fault the instant the scheduler started. | Push `task_trampoline`, which pops `func` and `arg` off the same stack and calls them. |
| Ring-3 frame one slot short | `kernel/task.c` | `context_switch` pops six callee-saved registers before reaching `iretq`, which then consumes five more. Only five zeros were pushed, so `iretq` loaded RIP from the CS value and jumped to address `0x1B` in ring 3. | Push six zeros so the frame is 6 register slots + the 5-slot `iretq` frame. |
| Boot gate could not see a crash | `tools/verify-boot.sh` | The gate only checked that init printed a line, so a kernel that completed init and then faulted passed. The two bugs above were invisible to CI. | Also assert liveness: QEMU exiting before the timeout fires, or a second firmware banner, both mean the guest reset. |

### Why this happened

The build already reported all of these. The warnings were visible in the
compiler output and were dismissed. The first corrective action taken was to
make CI verify something that could not distinguish "booted successfully" from
"halted immediately after the banner" — the old check greppped for `Built:`,
which is printed *before* the Multiboot2 magic is validated.

CI now asserts on `[BOOT] init complete, entering scheduler`, a line emitted
only after the magic validates, every `init*()` returns, and the shell task is
created — **and** that the guest is still alive afterwards. A kernel that boots
and then crashes now fails the build.

---

## Userspace status

The QEMU boot gate verifies a ring-3 task enters through `iretq`, prints via
the DPL3 `int 0x80` interface, yields to a kernel task, resumes its saved
interrupt frame, prints again, and leaves the guest alive. The test task is
still compiled into the kernel image; this does not yet mean ELF binaries can
be loaded as isolated user programs.

The SYSCALL/SYSRET assembly path, libc, ELF-to-task handoff, complete user
pointer validation, and per-task kernel interrupt stacks remain incomplete.

---

## Recommended Next Steps

1. **Implement TCP/IP stack** - ARP, ICMP, TCP state machine in net.c
2. **Add wait()/waitpid()** - complete process lifecycle
3. **Wire Settings app** - call config_set_* + config_save()
4. **Add -Werror to Makefile** - prevent future compile regressions
5. **Wire vmm_run_tests() at boot** - with RUN_VMM_TESTS=1 build target
6. **Implement sys_read/sys_write capability checks** - VFS path
7. **Add DHCP client** - for net_enabled=true to get real IP