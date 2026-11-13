# TODO - CharisOS Development Roadmap

## Phase 1: Memory/VMM Refactor (COMPLETE)
- [x] heap.c - separate heap module with kmalloc/kfree
- [x] bootmem.c - multiboot parsing skeleton
- [x] memory.c - thin orchestrator
- [x] vmm_walk(), vmm_create_address_space(), vmm_copy_kernel_mappings(), vmm_switch()
- [x] vmm_map_page_pml4() - map into specific PML4
- [x] PMM safety: double-free check, next-fit cursor

## Phase 2: Syscall Interface & User Space (PARTIAL)
- [x] SYSCALL/SYSRET assembly entry and MSR setup (user bootstrap wrappers currently use DPL3 `int 0x80`)
- [x] sys_exit(), sys_getpid(), sys_yield(), sys_sleep(), sys_waitpid()
- [x] sys_read()/sys_write() with fd-based I/O
- [x] sys_print() - debug output
- [ ] sys_fork() - create and validate child register context, address-space ownership and resources
- [x] task_create_with_pml4() - create task with custom address space
- [x] Per-process fd table (stdin/stdout/stderr pre-opened)
- [x] Preflight the full writable ring-3 `SYS_READ` destination before keyboard/VFS reads; host regression covers unmapped, read-only, cross-page and zero-length cases

## Phase 3: Filesystem (PARTIAL)
- [x] VFS layer (vfs.h, vfs.c) - vfs_node_t, mount points
- [x] Device nodes: /dev/null, /dev/zero, /dev/kbd, /dev/vga
- [x] Per-task descriptor table with fd_alloc(), fd_acquire()/fd_release() and fd_close()
- [x] Explicit per-descriptor read/write rights, checked independently of process capabilities
- [x] Task-bound opaque descriptor handles with owner-PID binding and per-slot generations
- [x] Reject stale handles after close/reopen, cross-task handle reuse and generation wrap
- [x] Make task PID allocation atomic and monotonic; fail closed at exhaustion without identity reuse
- [x] Reserve a descriptor invalid sentinel in the never-issued PID-zero namespace; verify the maximum PID/generation/slot tuple remains valid
- [x] Audit built-in task grants; remove unused filesystem/process syscall capabilities from kernel workers
- [x] Host concurrency regression verifies unique PIDs across parallel allocators
- [x] Recheck `CAP_FS_READ` on non-console descriptor reads
- [x] Reject zero, unknown and unavailable rights; standard streams cannot be closed through sys_close
- [x] Revoke rights on close and reset descriptor metadata on task exit and task-slot reuse
- [x] Host regression tests for rights, table capacity, task isolation and close ordering
- [x] Enforce global atomic ownership for closeable VFS nodes; reject cross-task and concurrent duplicate attachment, and serialize pointer reuse through close-callback completion
- [x] Close per-open VFS nodes in IL loader and Browser local-file paths; validate IL file size, instruction alignment and complete reads
- [x] Release VFS nodes through close callbacks when the ELF loader rejects a resolved node before descriptor allocation
- [x] Validate VFS callback byte counts in ELF, Browser and config readers; reject over-reported lengths and 64-bit offset overflow before use
- [x] Reject oversized 64-bit offsets in FAT32 VFS adapters before conversion to 32-bit `file_t.pos`; host regression proves no filesystem callback runs
- [x] FAT32 integration with VFS (read-only listing; mutation support remains incomplete)
- [x] sys_open() defaults to read-only; sys_open_rights() requests capability-backed rights
- [x] sys_close() rejects invalid, closed and standard descriptors
- [x] Specify the reference-counted open-file-description architecture, lifetime/authority invariants, lock-order requirements and adversarial regression matrix
- [x] Introduce kernel-owned open-file descriptions with packed no-resurrection reference lifecycle
- [x] Migrate syscall read/write and ELF loading to pin open-file descriptions for the full I/O operation
- [x] Serialize shared file offsets and VFS callbacks with a per-open-file I/O lock
- [x] Keep descriptor publication/revocation atomic with reference acquisition; run destructors outside the descriptor-table lock
- [x] Preserve permanent shared standard-stream descriptions across task creation and cleanup
- [x] Add host regressions for pins surviving descriptor close, final close exactly once, independent open offsets, allocation rollback and concurrent final release
- [x] Add kernel-internal `fd_duplicate_to()` with kernel task-table recipient resolution and strict rights attenuation
- [x] Reject descriptor duplication into full, allocating, exiting or zombie recipient tasks
- [x] Verify shared open-file identity/offset, recipient cleanup and sender-close lifetime
- [x] Specify authenticated IPC endpoint, rendezvous, rights and transactional resource-transfer requirements
- [x] Replace global integer channel IDs with task-local owner-PID/generation/slot endpoint handles
- [x] Add kernel-owned endpoint pairs with independent SEND/RECEIVE rights and authenticated peer identity
- [x] Add bounded full-duplex queues with nonblocking send/receive, explicit FULL/EMPTY/BUFFER_SMALL outcomes and no silent truncation
- [x] Revoke task-owned endpoints and release channel references during EXITING teardown
- [x] Add kernel-only blocking receive with atomic waiter registration, message/peer-close wakeups and task-exit cancellation
- [x] Add dedicated QEMU scheduler coverage proving receiver BLOCKED before send, message wake, second blocking wait and peer-close wake
- [x] Extend QEMU waiter coverage to prove task-exit peer close and local-endpoint revocation cancellation while the receiver is BLOCKED; assert CANCELLED/PEER_CLOSED and reap both workers
- [ ] Add transactional descriptor attachments with queue-owned references and receiver rollback
- [ ] Keep ring-3 IPC and descriptor transfer denied until endpoint tests and user-memory preflight pass
- [ ] Replace global shared-memory IDs/raw kernel-pointer access with task-owned objects and explicit user mappings

## Phase 4: ELF Loader (IN PROGRESS)
- [x] Validate static ELF64 x86-64 `ET_EXEC` headers and program-header bounds
- [x] Copy `PT_LOAD` file contents, zero pages for BSS, and map user permissions
- [x] `sys_exec` implementation validates policy, loads segments and prepares a replacement IRET frame
- [ ] CI #775: dedicated `TEST_ELF_EXEC_SUCCESS` image authorizes only the bootstrap smoke task, boots from the FAT32 test disk, and must enter `TEST.ELF` and emit `ELF_EXEC_OK` (default image still verifies denial)
- [x] Build bounded `argc`/`argv`/`envp` vectors plus `AT_NULL` on the initial stack
- [ ] `ET_DYN` and relocations/interpreters
- [x] Make ELF loading and initial-stack construction one transaction; roll back successfully loaded PT_LOAD pages if stack setup fails
- [ ] Add a forced stack-setup-failure regression that proves PT_LOAD frames are reclaimed
- [x] Successful exec replaces the process with a fresh address space and reclaims the prior user image only after switching CR3

## Phase 5: Graphics & Display (COMPLETE)
- [x] fb.h/fb.c - framebuffer driver using the Multiboot2 framebuffer handoff
- [x] psf.h/psf.c - PC Screen Font renderer (PSF2 support)
- [x] fb_put_pixel(), fb_clear(), fb_fill_rect()
- [x] graphics.h/graphics.c - 2D drawing primitives
- [x] graphics_line() - Bresenham's algorithm
- [x] graphics_rect() - filled/outline
- [x] graphics_circle() - midpoint circle
- [x] graphics_put_string() - PSF-based text rendering

## Phase 6: Window System (PARTIAL)
- [x] Window/widget/input foundations exist; WM bounds window count, dimensions, allocation size, title strings, clipping and z-order
- [x] PS/2 mouse input and pointer-driven shell interaction
- [x] Host-side WM lifecycle and bounds regression tests
- [x] Bufferless shell-surface registration without duplicate pixel buffers
- [x] Metadata-level move, transactional resize, minimize, restore, visibility and focus-cycle APIs with host tests
- [x] Independent Terminal, Files, Browser, Settings and About surface records with shell-owned render callbacks
- [x] Back-to-front composition of clipped per-window renderers without clearing the desktop/taskbar
- [x] Per-window mouse hit testing, focus-only key/pointer dispatch and titlebar dragging
- [x] Taskbar focus/minimize/restore and Alt+Tab focus cycling
- [x] Settings and About promoted from modal overlays to independent WM windows
- [x] Visible active-window focus rule and focused-window-only compositor regression
- [x] Bounded lower-right pointer resize handle with app-specific minimum dimensions
- [x] WM maximize/restore geometry snapshots with transactional allocation-failure behavior
- [x] Super+Up maximize, Super+Down restore/minimize and Alt+F4 close for focused apps
- [ ] Snap/tiling interactions
- [x] QEMU validation of simultaneous Files/Terminal windows and Alt+Tab input routing
- [x] QEMU keyboard maximize/restore verification (Super+Up / Super+Down)
- [x] QEMU keyboard resize verification (Alt+F8, arrows, Enter/Escape) through shared bounded geometry
- [ ] QEMU pointer-drag resize verification with a reliable PS/2 or hardware-input test
- [x] Add direct Files, Terminal, Settings and Browser launch cards with responsive one-/two-/four-column geometry and a live PIT/PMM System Overview panel
- [x] Add a lower System Status panel with live memory-allocation and display-profile values
- [x] Add a Kernel Activity panel with O(1) context-switch, interrupt and syscall counters; suppress it when the selected text scale or available geometry cannot accommodate it
- [x] Capture and verify the 1024×768 workspace, native app windows, multi-window focus routing, maximize/restore, keyboard resize, System Status, Kernel Activity and settings persistence across reboot in CI run #732
- [ ] Use the remaining desktop area for useful user-facing content; the verified system panels are not a substitute for a richer workspace
- [ ] Validate the full font/UI scale matrix and additional 1280×800 and 1920×1080 framebuffer modes

The shell still owns the framebuffer and application renderers, while the WM owns window identity, z-order, focus, clipping and composition. This is a functional multi-window foundation, not a mature desktop environment.

## Phase 7: IPC & Signals (PARTIAL)
- [x] Signals (SIGINT, SIGSEGV, SIGCHLD) - signal.c exists
- [x] Pipes - pipe.c implemented
- [x] Kernel-only fixed shared-memory block allocator exists; global IDs/raw pointers are not exposed to ring 3
- [ ] Replace global shared-memory IDs with task-owned objects, explicit user mappings and revocation
- [x] Remove global integer IPC channel IDs and legacy syscall wrappers/handlers
- [x] Add task-local generation-protected endpoint handles, kernel-established peer identity and independent SEND/RECEIVE rights
- [x] Add bounded full-duplex queues (16 messages per direction, 512 bytes per message), explicit FULL/EMPTY/BUFFER_SMALL/PEER_CLOSED results and non-truncating receive
- [x] Revoke endpoint handles during TASK_STATE_EXITING and preserve peer lifetime until its own endpoint closes
- [x] Host regression covers wrong-owner handles, directional rights, stale generations, generation exhaustion, aliased outputs, queue capacity, peer close, task teardown and endpoint-table limits
- [x] Add kernel-only cancellable blocking receive, atomic waiter registration, message/peer-close wakeups and task-exit unlinking
- [x] Add dedicated QEMU scheduler coverage proving receiver BLOCKED before send, message wake, second blocking wait and peer-close wake
- [x] Extend the QEMU waiter gate to cover task-exit peer close and cancellation by revoking a blocked reader's local endpoint; assert CANCELLED/PEER_CLOSED and reap both workers
- [x] Add a bounded kernel-only service namespace with exact-name registration, CAP_IPC_SERVICE ownership, client-capability policy, generation-protected request handles, blocking connect/accept/reject and task-exit/unregister cleanup
- [x] Add dedicated QEMU coverage for a server blocked in accept, client connect wakeup, rights-limited endpoint messaging, request rejection and unregister waking a genuinely blocked client
- [ ] Add transactional descriptor attachments, reference pinning, recipient-slot reservation and rollback
- [ ] Only design ring-3 IPC syscalls after user-memory preflight, service authorization and QEMU lifecycle tests exist
- [ ] Unix domain sockets (socket.c is INET-only)
- [x] Distinguish unused task slots from exited zombies; exited tasks are no longer silently reused
- [x] Preserve the `SYS_EXIT` status in zombie metadata for future `waitpid()` collection
- [x] Track each task's creator PID and provide task-table PID lookup for child collection
- [x] `SYS_WAITPID` supports exact-child and any-child selection, `WNOHANG`, validated status copy-out, blocking wakeup, and zombie-slot reclamation after kernel-stack release
- [x] Reparent descendants when a parent exits; auto-reap orphaned descendants only after their kernel stacks are detached
- [x] QEMU regression covers exact PID, any child, exit status, WNOHANG, invalid/non-child selectors, slot reclamation and orphan auto-reaping

## Phase 8: Network Stack (PARTIAL - ENABLED BUT INCOMPLETE)
- [x] Socket API (AF_INET, SOCK_STREAM/SOCK_DGRAM) - socket.c
- [x] Initialize the fixed kernel socket table before registering socket syscalls; host regression covers initial allocation, capacity and slot reuse
- [x] Socket syscalls (SYS_SOCKET, SYS_CONNECT, SYS_BIND, SYS_LISTEN, SYS_ACCEPT, SYS_SEND, SYS_RECV, SYS_SOCKET_CLOSE)
- [x] RTL8139 polling Ethernet TX/RX path and bounded frame validation
- [x] ARP replies, bounded neighbour cache and next-hop resolution
- [x] IPv4 header validation, fragment rejection and ICMP echo replies
- [x] UDP send/receive dispatch with checksum validation
- [x] Static IPv4 address, subnet mask and optional gateway configuration
- [x] Bounded asynchronous DNS A-record resolver
- [x] Minimal single-connection TCP client with basic handshake, ACK/data/FIN handling
- [x] Plain HTTP GET composition and bounded response-header/body handling for Browser
- [x] Bounded DHCPv4 DISCOVER/OFFER/REQUEST/ACK client, server-ID and BOOTP identity validation, router/subnet/DNS options, T1 unicast renewal, T2 broadcast rebinding and expiry cleanup
- [ ] Physical LAN DHCP interoperability, relay-agent and packet-loss/reconnect stress tests
- [ ] IPv6, neighbour discovery and ICMPv6
- [ ] Robust TCP retransmission, congestion control and concurrent connections
- [ ] TLS/HTTPS and certificate validation
- [ ] IRQ-driven RX or bounded DMA/ring processing with hardware stress tests
- [ ] Real-LAN interoperability tests; QEMU success alone is not proof of Internet access

## Phase 9: Desktop Environment (PARTIAL, ACTIVE)
- [x] Minimal workspace replaces the dashboard-style home surface; Files desktop shortcut, launcher and taskbar render in QEMU
- [x] Native Files launcher and read-only FAT32 root browser (8.3 names only)
- [x] Native Terminal surface with bounded built-in commands
- [x] Settings controls for font family, text size and global UI scale
- [x] Settings persist to `/CHARISOS.CFG` on supported primary ATA FAT32 and reload after reboot in CI
- [x] QEMU capture verifies the desktop after closing Settings at 200% text and 120% interface scale without the previous welcome-card clipping
- [x] Add a responsive one-/two-/four-column workspace shortcut grid for Files, Terminal, Settings and Browser with bounded hit testing
- [x] Add verified live system status indicators (uptime and PMM memory counters) and a labelled workspace hierarchy
- [x] Add scale-aware workspace geometry and suppress secondary card descriptions at larger font scales
- [x] Distribute shortcut cards across responsive one-, two- or four-column layouts with bounded card width (fresh QEMU visual verification pending)
- [ ] Verify card and System Overview typography at 100/125/150/200% text/UI scales; add measured wrapping where clipping remains
- [ ] Implement shared layout primitives: measure/arrange, horizontal/vertical flow, padding/gaps, wrapping, clipping/ellipsis and scroll containers
- [ ] Verify layout at 100/125/150/200% text/UI scales and 1024x768, 1280x800 and 1920x1080 framebuffer sizes
- [x] Standardize native app titlebars, close controls, focus indication and keyboard dispatch for current applications
- [x] Add independently movable windows, multi-window z-order, taskbar focus/restore and Alt+Tab
- [x] Add pointer resize handles, maximize/restore and focused-window keyboard shortcuts
- [x] QEMU capture verifies multiple application windows, Alt+Tab routing, maximize/restore and keyboard resize
- [x] Desktop workspace exposes direct Files, Terminal, Settings and Browser shortcuts
- [ ] Add snap/tiling, global window menu, QEMU pointer-resize test and consistent per-window keyboard navigation
- [ ] Add a first-login setup wizard with language/keyboard, appearance and network steps; current wizard remains limited to existing setup preferences
- [ ] Add functional Text Editor and System Monitor applications

## Phase 10: User Applications (ACTIVE)
- [x] Native framebuffer Terminal window with keyboard input and shell/Start-menu integration
- [x] Built-in commands: help, clear, echo, uname, version, uptime, mem, tasks, settings
- [x] Native Files application: read-only FAT32 root listing, selection and refresh
- [ ] Terminal text selection, clipboard integration and standard window chrome
- [ ] Files write operations, nested directories, long filenames and safe mutation
- [ ] Text Editor with bounded open/edit/save workflow
- [ ] System Monitor with real CPU, memory and process data
- [ ] Calculator with validated arithmetic
- [ ] Game SDK with graphics/audio syscalls; no actual games yet

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
- [x] Reject fixed-layout gamepad input reports shorter than the highest byte consumed; address/undefined sanitizer regression covers the 8-byte boundary
- [x] Game SDK header (sdk/charis_game.h)
- [x] SYS_GAME_* syscalls for graphics/audio - registered but minimal impl
- [ ] Actual game demo

## Phase 14: Security & Capabilities (PARTIAL - ENFORCEMENT IN PROGRESS)
- [x] Capability bitmask per task (CAP_FS_READ, CAP_FS_WRITE, CAP_SPAWN, etc.)
- [x] Capability checks require every requested bit; zero and undefined masks fail closed
- [x] Sanitize task capability grants against the kernel-defined capability mask at construction
- [x] Host regression tests cover single-bit, multi-bit, empty, unknown and sanitized capability requests
- [ ] Capability inheritance and reduction rules for a future working fork implementation
- [x] Capability checks on `sys_open`, non-console `sys_write`, and `sys_exec` authorization (the bootstrap task intentionally lacks `CAP_ALLOW_EXEC`)
- [x] Path traversal blocking in security_verify_path() (NEW)
- [ ] Capability enforcement in VFS write/read paths
- [ ] Capability enforcement in socket/net syscalls
- [ ] Cryptographically secure entropy source; token generation remains disabled when RDRAND is unavailable
- [ ] Persistent audit log
- [x] Allocate each kernel task stack from page-aligned supervisor-only writable/NX frames, with a genuinely unmapped guard page and deferred physical-frame reclamation after context switch
- [x] Configure a dedicated, aligned TSS.IST1 landing stack for page-fault vector 14 and verify the exception frame remains within it
- [x] QEMU deliberately writes into a kernel task's unmapped stack guard; CI run #732 verifies IST1 frame placement and the diagnostic panic markers
- [ ] Add a separate IST2/double-fault stack and nested-fault regression; the guard test halts safely rather than attempting unsafe recovery of a damaged kernel task

## Phase 15: Configuration & Persistence (PARTIAL, QEMU VERIFIED)
- [x] In-memory typed config store
- [x] Root FAT32 config file /CHARISOS.CFG created when a writable primary ATA volume is mounted
- [x] Settings UI saves font family, font scale and UI scale immediately
- [x] CI reads the saved file, reboots with the same disk and verifies the values reload
- [ ] Persistence on non-ATA storage and filesystems beyond current FAT32 support

## Phase 16: Build & Quality (IN PROGRESS)
- [x] Kernel compiles cleanly (all 7 errors fixed)
- [x] CI workflow (.github/workflows/ci.yml) - build + QEMU ring-3 yield/resume boot test
- [x] QEMU regression gate covers BIOS ring-3 yield/resume, user-fault containment and ELF capability-denial policy
- [ ] QEMU regression gate authorizes and successfully enters a static ELF program
- [x] Repo hygiene: .gitattributes, .gitignore, CRLF→LF fixed
- [x] Boot debug markers fixed (no collisions, full coverage)
- [ ] Make the default `make test` target execute the VMM suite; CI currently boots a separate `RUN_VMM_TESTS` image and runs the full regression suite in QEMU
- [x] Removed obsolete `disk.c` implementation
- [x] Enable `-Werror` for kernel compilation
- [x] Add host-side WM lifecycle/bounds regression coverage to CI
- [x] Host test proves only focused visible windows receive dispatched events
- [x] Host test verifies the focused window receives the visible compositor focus rule
- [x] Host tests cover maximize/restore geometry, hidden/minimized rejection and transactional buffer allocation failure
- [x] WM list traversal detects duplicate nodes and remains bounded if links form a cycle
- [x] Build against the generic x86-64 baseline and disable BMI/BMI2 code generation
- [x] CI disassembly gate rejects accidental BMI/BMI2-family kernel instructions
- [x] CI run #732 passed the freestanding build, x86-64 baseline audit, BIOS/ring-3 isolation checks, VMM huge-page/permission regression suite, dedicated kernel-stack guard fault on IST1 and QEMU desktop/settings capture
- [x] QEMU capture from run #732 verifies Files, Terminal, Browser, multi-window routing, maximize/restore, keyboard resize, System Status and Kernel Activity counters
- [x] QEMU guard-probe from run #732 verifies the kernel stack-guard #PF arrives on IST1 and produces the expected diagnostic markers
- [x] FAT32 settings persistence is read back and verified after reboot (`font_scale=2`, `ui_scale=120`); the capture also checks the largest font preset
- [ ] Expand automated regression coverage to pointer-drag resize, snap/tiling, real hardware and long-session desktop stability

## Phase 17: Process Management (IN PROGRESS)
- [x] Bootstrap user task enters ring 3 with a private address-space branch and mapped user stack
- [x] DPL3 syscall, yield to a kernel task, and resume the user interrupt frame (QEMU verified)
- [x] Static ELF loader and ring-3 frame-replacement implementation exist
- [ ] CI #775 verifies successful authorized ELF entry through the dedicated exec-test image; default build remains denied by `CAP_ALLOW_EXEC`
- [ ] Complete user ABI/libc and validate SYSRET path
- [ ] Per-task kernel interrupt stacks and SMP-safe TSS handling
- [x] `waitpid()` with exact-child/any-child selection, `WNOHANG`, validated status copy-out, deferred-stack-safe zombie publication, slot reclamation and orphan adoption; dedicated QEMU regression coverage
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
| 5 | Dead code and VMM test coverage | ⚠️ `disk.c` removed; VMM tests compile and run in a dedicated QEMU CI image |
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
| `char c` compared `> 127` | `kernel/psf.c:30,62` | `char` is signed on x86, so `c > 127` is always false. Latin-1/high-byte characters are never skipped and index out of the intended glyph range. | Cast input to `unsigned char` before range checks. Fixed in `kernel/psf.c`. |
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

1. **Transactional IPC transfer** - add descriptor attachments, queue-owned open-file references, recipient-slot reservation and rollback; keep ring-3 IPC disabled until user-memory preflight and authorization tests exist
2. **IPC adversarial lifecycle** - add QEMU coverage for service-owner exit during blocked accept, concurrent clients/acceptors, cancellation/timeout policy and request-table pressure before any public ABI exists
3. **Internet connectivity** - validate DHCP, route/DNS installation, lease renewal and reconnect behavior against real LAN DHCP servers, then complete HTTPS/TLS prerequisites
4. **Secure transport** - add TLS with certificate and clock validation before presenting the Browser as Internet-ready
5. **Hardware services** - add timezone-aware RTC policy, ACPI battery discovery and reliable HDA PCM playback
6. **Wireless networking** - define a supported adapter matrix, then implement and test the first Wi-Fi driver without claiming generic support
7. **Storage and desktop quality** - finish FAT32 write safety and writable Files UI, verified CC0 wallpaper loading, font/scale geometry, real device status and QEMU capture publication

## Implementation Details (merged from PROGRESS.md, source of truth)

- `kernel/irq.c`: `pic_init`, `pic_send_eoi`, `pic_mask_irq`, `pic_unmask_irq`; EOI added to timer and keyboard handlers
- `kernel/input.c`: mouse button state tracking; `wm_process_mouse` for window dragging
- `kernel/services.c`: background service manager (not yet reflected in a TODO phase)
- `kernel/display.c`: multi-monitor support (up to 4 displays; not yet in TODO)
- `kernel/power.c`: power state management (idle, sleep, shutdown; not yet in TODO)

> PROGRESS.md was removed on 2026-10-05. TODO.md is the single source of truth.

## Phase 10: Hardware Services & Desktop Media (PLANNED)
- [x] Stable CMOS RTC sampling with UIP checks, repeated-sample consistency, BCD/binary and 12/24-hour decoding, Gregorian validation and epoch conversion
- [x] Seed wall-clock epoch once at boot and advance it using monotonic PIT elapsed time; taskbar refresh repaints only the taskbar
- [ ] UTC storage policy, timezone configuration/conversion and network time synchronization
- [ ] ACPI discovery and battery telemetry (charge, state, remaining capacity/time when firmware exposes it)
- [ ] Complete HDA codec/controller initialization, DMA ring handling, PCM playback, mixer and safe stream lifecycle
- [ ] Wi-Fi device discovery and a driver architecture; support a documented first adapter family before claiming wireless support
- [x] DHCPv4 lease acquisition and renewal integrated with the wired network worker
- [ ] Physical LAN DHCP, DNS reachability, route installation and reconnect behavior verified on supported hardware
- [ ] Real Internet path verification: DHCP/static config, DNS, HTTP, TLS, timeouts, packet loss and reconnect behavior
- [x] Shortlist and record high-resolution CC0 wallpaper sources with source pages, author and original dimensions; see assets/wallpapers/README.md
- [ ] Download and vendor the source files, record checksums and re-verify files after download
- [ ] Wallpaper asset pipeline with image dimension limits, safe decoding and aspect-ratio-aware scaling
- [ ] High-quality bundled wallpaper set with no embedded logos, watermarks or UI overlays; user-selectable wallpaper and fallback procedural themes
- [ ] Desktop capture pipeline publishes the actual QEMU framebuffer capture and keeps README screenshots synchronized with the tested commit
- [ ] Native terminal visual pass: near-black console canvas, compact chrome, dedicated monospace font metrics, carefully measured CharisOS ASCII banner and stable prompt/output layout
- [ ] Verify ASCII banner width/height against font-cell metrics at all supported text scales and narrow window sizes
- [x] Add host regressions for RTC BCD/binary modes, 12-hour conversion, invalid dates, leap days and Unix epoch/calendar conversion
- [ ] Add QEMU checks for RTC-present and RTC-absent boot, taskbar date/time rollover and long-uptime monotonic behavior
- [ ] Add hardware/service tests for timezone changes, missing battery devices and audio device absence
