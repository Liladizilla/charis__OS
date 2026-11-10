# TODO - CharisOS Development Roadmap

## Phase 1: Memory/VMM Refactor (COMPLETE)
- [x] heap.c - separate heap module with kmalloc/kfree
- [x] bootmem.c - multiboot parsing skeleton
- [x] memory.c - thin orchestrator
- [x] vmm_walk(), vmm_create_address_space(), vmm_copy_kernel_mappings(), vmm_switch()
- [x] vmm_map_page_pml4() - map into specific PML4
- [x] PMM safety: double-free check, next-fit cursor

## Phase 2: Syscall Interface & User Space (PARTIAL)
- [x] SYSCALL/SYSRET assembly entry and MSR setup (user bootstrap wrappers currently use DPL3 int 0x80)
- [x] sys_exit(), sys_getpid(), sys_yield(), sys_sleep()
- [x] sys_read()/sys_write() with fd-based I/O
- [x] sys_print() - debug output
- [x] sys_fork() - creates child with separate PML4
- [x] task_create_with_pml4() - create task with custom address space
- [x] Per-process fd table (stdin/stdout/stderr pre-opened)

## Phase 3: Filesystem (PARTIAL)
- [x] VFS layer, device nodes, per-task fd table and read-only FAT32 integration
- [ ] Capability enforcement in all VFS read/write paths

## Phase 4: ELF Loader (IN PROGRESS)
- [x] Validate static ELF64 x86-64 ET_EXEC headers and program-header bounds
- [x] Copy PT_LOAD contents, zero BSS and map user permissions
- [x] sys_exec replaces the ring-3 IRET frame with the ELF entry and stack
- [x] QEMU FAT32 smoke test enters an ELF and prints ELF_EXEC_OK
- [x] Build bounded argc/argv/envp vectors plus AT_NULL
- [ ] ET_DYN, relocations/interpreters and safe image teardown on repeated exec

## Phase 5: Graphics & Display (COMPLETE)
- [x] Multiboot2 framebuffer, PSF2 bitmap font and 2D graphics primitives

## Phase 6: Window System (PARTIAL)
- [x] Bounded WM window count, dimensions, buffer allocation, title sanitization, clipping and z-order
- [x] Host-side lifecycle and bounds tests
- [x] Bufferless shell-surface registration without duplicate pixel buffers
- [x] Metadata-level move, transactional resize, minimize, restore, visibility and focus-cycle APIs with host tests
- [ ] Wire geometry operations to shell-owned application renderers
- [ ] Independent per-window render surfaces and compositor ownership
- [ ] Titlebar close/minimize/maximize controls and resize handles
- [ ] Alt+Tab and taskbar focus/restore integration
- [ ] QEMU tests for overlapping windows, pointer drag, resize and focus transitions

The current shell still owns the framebuffer and supports one modal app at a time. WM metadata operations do not yet constitute a complete multi-window desktop.

## Phase 7: IPC & Signals (PARTIAL)
- [x] Signals and pipes foundations
- [x] Shared memory foundation
- [ ] Unix domain sockets
- [x] Track exited tasks as zombies and preserve exit status/creator PID
- [ ] wait()/waitpid() and safe resource reaping

## Phase 8: Network Stack (PARTIAL)
- [x] AF_INET socket API and syscalls
- [x] RTL8139 PCI probe and basic transmit path
- [ ] TCP/IP, ARP/ICMP, receive path, DHCP and IRQ-driven processing

## Phase 9: Desktop Environment (PARTIAL, ACTIVE)
- [x] Minimal workspace, launcher, taskbar and native Files/Terminal/Settings surfaces
- [x] Settings font family, text size and UI scale persistence
- [ ] Improve desktop density, status indicators and accessible workspace affordances
- [ ] Shared layout primitives and scale/resolution validation
- [ ] Independent multi-window desktop and consistent native app chrome
- [ ] First-login setup wizard with language, keyboard, appearance and network steps
- [ ] Functional Text Editor and System Monitor

## Phase 10: User Applications (ACTIVE)
- [x] Native Terminal with bounded built-in commands
- [x] Native Files with read-only FAT32 root listing
- [ ] Terminal selection/clipboard and standard window chrome
- [ ] Files writes, nested directories, long filenames and safe mutation
- [ ] Text Editor, System Monitor and Calculator

## Phase 11: Hardware Abstraction (PARTIAL)
- [x] HDA probe foundation, USB enumeration placeholder and PCI driver framework
- [ ] ATA identify/write support and broader GPU/framebuffer driver support

## Phase 12: Graphics Acceleration (PARTIAL)
- [x] Software rasterizer and triangle/depth-buffer foundation
- [ ] Integrate rasterizer with window system

## Phase 13: Gaming Support (HEADERS/FOUNDATIONS)
- [x] Gamepad and game SDK/syscall foundations
- [ ] Actual game demo

## Phase 14: Security & Capabilities (PARTIAL)
- [x] Per-task capability bitmask and inheritance
- [x] Capability enforcement in sys_open/sys_exec and path traversal blocking
- [ ] Enforce capabilities across VFS and network syscall paths
- [ ] Cryptographically secure entropy source; token generation remains disabled without RDRAND
- [ ] Persistent audit log

## Phase 15: Configuration & Persistence (PARTIAL)
- [x] Typed in-memory config and FAT32 settings persistence
- [ ] Persistence on other storage devices/filesystems

## Phase 16: Build & Quality (IN PROGRESS)
- [x] Kernel builds with warnings treated as errors
- [x] CI boot, ring-3, ELF, VMM, desktop capture and WM host tests
- [ ] Wire remaining VMM tests into a unified host/QEMU test target
- [ ] Expand malformed filesystem, syscall and input regression coverage

## Phase 17: Process Management (IN PROGRESS)
- [x] Ring-3 bootstrap, DPL3 syscall/yield/resume and static ELF execution smoke test
- [ ] Complete user ABI/libc, SYSRET validation and per-task kernel interrupt stacks
- [ ] wait()/waitpid(), process groups, signals and resource limits

## Security posture
CharisOS remains an early-stage kernel. Capability enforcement, user-pointer validation, address-space teardown, interrupt-stack isolation, filesystem mutation and cryptographic entropy are incomplete. Do not describe it as production-secure or a Windows replacement until these areas have been implemented and independently tested.
