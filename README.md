# CharisOS

**A native x86_64 operating system built from the ground up in freestanding C and NASM.**

[![CI](https://github.com/Liladizilla/charis__OS/actions/workflows/ci.yml/badge.svg)](https://github.com/Liladizilla/charis__OS/actions/workflows/ci.yml)
![Architecture](https://img.shields.io/badge/architecture-x86__64-informational)
![Languages](https://img.shields.io/badge/kernel-C%20%2B%20NASM-informational)
![Boot](https://img.shields.io/badge/boot-GRUB2%20%2F%20Multiboot2-informational)
![License](https://img.shields.io/badge/license-MIT-informational)

Maintained by **Charis Chara** · [Source repository](https://github.com/Liladizilla/charis__OS)

---

## Project overview

CharisOS is an operating system targeting x86_64 PCs and virtual machines, with a kernel, boot path, memory management, scheduling, storage, networking foundations and a native framebuffer desktop. The repository contains a freestanding kernel, Multiboot2/GRUB boot path, physical and virtual memory management, interrupt and task infrastructure, a small syscall interface, FAT32-backed storage support, a static ELF executable loader, and a framebuffer-based graphical shell.

CharisOS is an operating system under active development. Its kernel boots and runs directly on x86_64 hardware and virtual machines, but the system is not yet a general-purpose or production-ready desktop OS. Several hardware paths, security boundaries, userspace facilities and everyday applications remain incomplete. This README distinguishes implemented and tested behavior from partial subsystems and planned work; compiling a component alone is not evidence that it works end to end.

**Implementation and boot boundary:** the kernel and boot assembly are implemented in C and NASM. The kernel is compiled as a freestanding x86_64 image with GCC and a custom linker script; it does not link against a host operating system's libc or runtime, and it executes directly on the machine after the bootloader transfers control. It is not a self-booting firmware replacement: the current boot path relies on BIOS or UEFI firmware and GRUB2 with Multiboot2 to load the kernel and provide the initial boot information. This is the normal firmware → bootloader → operating-system-kernel boundary. The kernel supplies its own memory management, interrupt handling, scheduler and kernel services after handoff.

## Current state at a glance

| Subsystem | Current evidence-based status |
|---|---|
| BIOS boot | CI verifies GRUB/Multiboot2 boot from CD image and raw disk image in QEMU |
| UEFI boot | UEFI paths exist; CI coverage is advisory and firmware-dependent |
| CPU and memory | x86_64 long-mode entry, PMM, VMM, a single-CPU preemption-safe heap free list, and page-permission regression tests |
| Interrupts and scheduling | IDT/PIC/PIT, PS/2 input and kernel-task scheduling; ring-3 yield/resume exercised in QEMU |
| User mode | DPL3 `int 0x80` bootstrap path exercised; static ELF64 `ET_EXEC` loader uses transactional address-space replacement; a separate CI-only build now exercises authorized ELF entry |
| Storage | Primary ATA PIO and FAT32 support; current Files interface is read-only and limited to the root directory and 8.3 names |
| Graphical shell | Framebuffer workspace with independent Terminal, Files, Browser, Settings and About windows; workspace and application coverage remain limited |
| Window manager | Bounded ownership, back-to-front composition, focus-only input, move, lower-right resize grip, maximize/restore, minimize, taskbar switching and Alt+Tab; snap/tiling and QEMU pointer-resize validation remain unfinished |
| Settings | Font family, text size and interface scale; CI verifies saved values survive a reboot on a writable FAT32 test disk |
| Security | Selected capability checks and input validation; page faults use a dedicated TSS.IST1 stack with QEMU guard-fault verification; overall enforcement and isolation remain incomplete |
| Networking | RTL8139 polling Ethernet TX/RX; ARP, IPv4, ICMP, UDP, static routing, a DNS A-record resolver that authenticates the echoed question and answer owner, a constrained TCP client with strict single-segment ACK/FIN sequence checks, plain HTTP and a bounded DHCPv4 client with lease renewal are implemented. Real-LAN interoperability remains unverified; IPv6, TLS/HTTPS, robust multi-connection TCP and Wi-Fi are not implemented |
| Socket API | Kernel-only fixed 16-entry AF_INET stream/datagram facade; boot table initialization is tested. This is not a complete POSIX socket stack or a userspace socket ABI |
| Platform services | CMOS RTC is sampled at boot and exposed as a wall clock advanced from the monotonic PIT timer; timezone configuration, ACPI battery telemetry and complete sleep/hibernate are not implemented |
| Audio and peripherals | PC-speaker beep path and incomplete HDA work exist; general PCM playback is not supported. USB and gamepad support remain incomplete; the current fixed-layout gamepad parser rejects undersized reports, but descriptor-driven HID support is not implemented |
| Userspace | No complete libc, package manager, installer or general-purpose userspace environment |

For the authoritative subsystem-by-subsystem roadmap, see [TODO.md](./TODO.md). A subsystem is not considered complete merely because it compiles.

## Desktop captures

The primary image below is a **real 1024×768 QEMU framebuffer capture**, not a mockup. It was produced by the successful CI workflow run [43258397426](https://github.com/Liladizilla/charis__OS/actions/runs/43258397426) on the DHCP integration branch at `43c92a3`, then merged without source changes into `main` as `81e861e` on 12 November 2026.

![CharisOS desktop workspace, QEMU capture from 12 November 2026](docs/screenshots/charisos-desktop-2026-11-12.png)

The same run's [desktop preview artifact](https://github.com/Liladizilla/charis__OS/actions/runs/43258397426#artifacts) contains the Browser, Files, multi-window, maximized, resized, Settings, persisted-Settings-after-reboot, scaled-desktop and Terminal captures, together with serial and QEMU logs. The artifact is retained by GitHub Actions for a limited period; the desktop image above is checked into this repository.

### Latest desktop validation

The `Build, Boot & Ring-3 Test` job completed successfully in workflow run [43258397426](https://github.com/Liladizilla/charis__OS/actions/runs/43258397426). The QEMU capture step exercised the desktop, Browser, Files and Terminal launch paths; simultaneous Files/Terminal windows and Alt+Tab routing; maximize/restore and keyboard resize; display preference changes; and persistence of font and UI-scale values across a reboot using a writable FAT32 test disk.

The screenshot documents the current engineering state, not a finished commercial desktop. The workspace is compact and framebuffer-based; Files remains read-only and limited to the FAT32 root with 8.3 names; Terminal is a bounded kernel command environment; Browser supports plain HTTP only. The visual system, application depth, hardware coverage and end-user workflows remain active work. Successful CI is evidence of these specific tests, not proof of broad hardware compatibility or production readiness.

Older [run #724 desktop capture](docs/screenshots/charisos-desktop-run-724.png), [run #722 capture](docs/screenshots/charisos-desktop-run-722.png) and [historical preview gallery](docs/screenshots/charisos-preview-gallery.jpg) remain available for comparison.

## What is implemented

- GRUB2 and Multiboot2 boot path into x86_64 long mode.
- Physical memory bitmap allocator, virtual memory management and kernel heap.
- IDT, PIC, PIT, PS/2 keyboard/mouse, CMOS-seeded wall clock and diagnostic output.
- Kernel-task scheduling and a tested ring-3 bootstrap path.
- DPL3 `int 0x80` syscall dispatch and a bounded static ELF64 `ET_EXEC` loader. The loader builds a fresh process address space, loads segments and the initial stack there, switches CR3 only after success, then reclaims the previous image. A separate CI-only image now exercises successful authorized ELF entry; see the current CI gate for its result.
- VFS abstractions with a limited FAT32 backend over primary ATA PIO.
- Framebuffer drawing and PSF2 bitmap-font rendering.
- Native graphical workspace, launcher/taskbar, Terminal, Files, Browser, Settings and About. Each application is registered as an independent bufferless WM surface and painted inside its own clipped window rectangle.
- Terminal-specific fixed-cell monospace rendering, a near-black console and a 19-column CharisOS ASCII C mark. Host CI checks exact row dimensions; the terminal uses an independent fixed 8×16 monospace font so the startup identity remains visible even when desktop text is enlarged. The terminal remains a bounded kernel command environment, not a full userspace shell.
- Window-manager back-to-front composition, per-window focus and keyboard/pointer dispatch, titlebar dragging, a lower-right pointer resize grip, Alt+F8 keyboard resizing, maximize/restore, taskbar focus/minimize/restore, Alt+Tab cycling and visible focus rules. Host lifecycle, geometry, clipping, event-routing and allocation-failure tests cover the foundation; QEMU verifies simultaneous windows and keyboard maximize/restore.
- Persistent display preferences on supported writable FAT32 storage.
- Workspace System Status panel reads PMM allocator counters and the active display profile without walking task state during repaint.
- CI verifies the freestanding build, x86-64 baseline, BIOS/ring-3 path, VMM permission regressions, host-side WM tests and QEMU desktop/settings capture; [run #724](https://github.com/Liladizilla/charis__OS/actions/runs/43071788055) passed on commit `1f0414c`.

## Important limitations

CharisOS is a developing operating system whose kernel, userspace and desktop are still being built out. In particular:

- The desktop now supports independent Terminal, Files, Browser, Settings and About windows with z-order composition, per-window focus/input, titlebar dragging/resizing, keyboard resize, maximize/restore, taskbar switching and Alt+Tab. Snap/tiling behavior, dirty-region repaint and a richer workspace are still unfinished; this is a small kernel-owned shell, not a mature desktop environment.
- The workspace exposes direct shortcuts for Files, Terminal, Settings and Browser; the separate multi-window capture demonstrates Files and Terminal open together. Files is read-only, Settings currently exposes display preferences, and the desktop is not yet at release-quality visual or interaction completeness.
- Files does not yet support writable files, nested directory navigation or long filenames.
- The executable loader currently targets static ELF64 `ET_EXEC`; dynamic linking, relocations, process groups/sessions, signal delivery and resource limits remain unfinished. Parent/child lifecycle now includes `waitpid()` with exact-child/any-child selection, `WNOHANG`, validated status copy-out, deferred-stack-safe zombie publication, slot reclamation and orphan adoption, with dedicated QEMU regression coverage. A successful end-to-end authorized ELF boot on the full firmware matrix remains unfinished. Successful exec builds a new address space transactionally, preserves the old image on any failure, switches CR3 only after segment and stack construction succeed, and reclaims the old image after the switch. The default image still denies `CAP_ALLOW_EXEC`; a dedicated CI-only image now tests the authorized path.
- The `SYSCALL/SYSRET` user-mode path is configured but not yet validated as the normal syscall entry.
- Capability enforcement covers only selected operations. It is not a complete security boundary.
- The current entropy source is hardware-feature-dependent. If RDRAND is unavailable, cryptographic token generation is disabled; no weak fallback is presented as cryptographic randomness.
- Storage support is limited to legacy primary-channel ATA PIO and FAT32. AHCI and NVMe are not implemented.
- Networking has a polling RTL8139 Ethernet path with receive, ARP/IPv4/ICMP/UDP, static routes, DNS, a constrained TCP client, plain HTTP and DHCPv4 discovery, lease acquisition and renewal. It is not yet a general Internet-ready stack: IPv6, TLS/HTTPS, robust TCP retransmission/concurrency and Wi-Fi driver support are not implemented. Real-LAN DHCP interoperability and public Internet access remain unverified.
- The CMOS RTC seeds the wall clock at boot; the taskbar clock advances from PIT monotonic time without repeated CMOS polling. The service displays firmware-provided time as-is: timezone configuration, UTC conversion, clock discipline/NTP and ACPI battery telemetry are not implemented. Sleep/hibernate remain incomplete.
- Audio currently has a PC-speaker beep path and incomplete HDA work; general PCM playback is not supported. USB and gamepad support remain incomplete.
- The graphics path uses the currently supported framebuffer mode; broad hardware and resolution coverage remains future work.

Do not use this project to store sensitive data or as the only operating system on a machine.

## Documentation map

| Document | Purpose |
|---|---|
| [TODO.md](./TODO.md) | Current implementation status, milestones and remaining work |
| [docs/charis-design-language.md](./docs/charis-design-language.md) | Desktop visual principles, scaling goals and current UI limitations |
| [SECURITY.md](./SECURITY.md) | Security reporting and responsible-disclosure guidance |
| [docs/RELEASE_INTEGRATION.md](./docs/RELEASE_INTEGRATION.md) | Mainline branch discipline, test evidence and ISO release provenance |
| [LICENSE](./LICENSE) | MIT license |

---

## Architecture

### Boot sequence

```
BIOS/UEFI
   │
   ▼
GRUB2  ── multiboot2 /boot/kernel.elf ──▶  EAX=0x36d76289, EBX=mboot info
   │
   ▼
boot/boot.asm          32-bit protected mode
   ├─ saves EAX/EBX first (before any debug output clobbers them)
   ├─ CPUID: cpuid supported / long mode / APIC
   ├─ builds PML4 → PDPT → PD, identity-maps the first 4 GiB with 2 MiB pages
   ├─ enables PAE + LME + NX + PG, loads GDT + TSS
   ▼
boot/long_mode.asm     64-bit
   ├─ loads segment selectors, clears registers
   ├─ rsp ← stack_top
   └─ call kernel_main(mb_magic, mb_info)
   ▼
kernel/main.c :: kernel_main()
   ├─ vga_init, serial_init, Multiboot2 magic validation
   ├─ security_init (seeds stack guard; cryptographic tokens stay disabled without RDRAND)
   ├─ memory_init: Multiboot map → PMM → VMM/direct map → kernel heap
   ├─ fb_init → idt_init → input_init
   ├─ irq_init → timer_init(1000) → keyboard_init → ata_init → fs_init → vfs_init
   ├─ task_init → scheduler_init → syscall_init → graphics_init → mouse_init
   ├─ install UI input hooks → wm_init → ipc_init → audio_init → signal_init → pipe_init
   ├─ driver_init → pci_scan → driver_scan_and_bind
   ├─ config_init → config_load("/CHARISOS.CFG") → apply display preferences
   ├─ display_init → desktop_init → services_init → diag_init → power_init
   ├─ net_init (gated on config "net_enabled") → apps_init
   ├─ create ring-3 bootstrap and either the framebuffer desktop or text shell task
   └─ sti; scheduler_start()
```

### Boot markers

While the kernel initialises, `kernel_main()` writes a single character per
subsystem directly into the VGA text buffer at `0xB8000`, so a hang is
identifiable even before the serial/VGA console paths are trustworthy. A
self-incrementing helper (`BOOT_MARK`) guarantees each marker lands on its own
2-byte-aligned cell, so markers can never collide or corrupt a neighbour.

```
K V S M m I Q T K D F v t S C G W i p a s P d P b c L D d S D p S n N n
```

### Memory layout

```
0x00000000 ┌──────────────────────────────┐
           │ Real mode / BIOS / GRUB      │
0x00100000 ├──────────────────────────────┤  kernel loaded here (ENTRY(start))
           │ .multiboot   Multiboot2 hdr │  PT_LOAD  R-X   (segment "text")
           │ .text        code           │  _text_start … _text_end
           │ .rodata      constants      │  PT_LOAD  R--   (segment "rodata")
           │ .data        initialised    │  PT_LOAD  RW-   (segment "data")
           │ .bss         zero-filled    │  4096-aligned
           └──────────────────────────────┘  _end — PMM starts allocating here
```

Three PT_LOAD segments with distinct R-X / R-- / RW- permissions.

### Subsystem dependencies

```
types.h ─┬─ vga.c      serial.c     string.c
         └─ memory.c ─┬─ pmm.c  vmm.c  heap.c  bootmem.c
                       ├─ idt.c + irq.c ── timer.c, keyboard.c
                       ├─ task.c ── scheduler.c ── syscall.c
                       │                              ├─ vfs.c + fs.c + ata.c
                       │                              ├─ elf.c          (sys_exec)
                       │                              └─ ipc.c + pipe.c + socket.c ── net.c
                       └─ fb.c + psf.c ── graphics.c ── compositor.c ── wm.c ── desktop.c
                       pci.c ── driver.c ── hda.c, usb.c
                       config.c     security.c     diagnostics.c
```

### Syscall ABI

`SYSCALL` with the target configured in `IA32_LSTAR`, a `0xC0000080`-based
`IA32_STAR` for the ring-3 selectors, and `IA32_FMASK` to mask RFLAGS.
`syscall_dispatch()` looks the handler up in a 256-entry table.

Numbering follows Linux where the two overlap (`READ`=0, `WRITE`=1, `OPEN`=2,
`CLOSE`=3, `GETPID`=39, `FORK`=57, `EXEC`=59, `EXIT`=60, `WAITPID`=61) and uses a
CharisOS-specific band above 70 for sockets, diagnostics and the game SDK.
IPC syscall slots 91-93 are reserved and unregistered; authenticated ring-3
endpoint operations are not part of the current syscall ABI.
`WAITPID(pid, status, options)` accepts an exact child PID or `-1` for any child;
`WNOHANG` is the only supported option. The status destination is validated
before the kernel blocks or consumes a zombie.
This is a custom ABI — it is **not** Linux-ABI compatible.

### Scheduling

The ready queue is a singly-linked list. `scheduler_find_next()` walks it and
selects the first `TASK_STATE_READY` task, so blocked tasks stay in place and are
skipped rather than dequeued. Simple and correct for the current 32-task
ceiling, but every tick is O(n) over all tasks, so it would need a real priority
structure before the task limit is raised.

### Process lifecycle

Each task records the PID of the task that created it. `WAITPID` can collect
only a direct child, returns its exit status, and recycles the fixed task-table
slot only after the scheduler has switched away from and released the child's
supervisor-only kernel stack. A blocked parent is woken for a matching child
exit; the child is not published as reapable until that deferred release has
completed. Parents that exit reparent live descendants to the kernel adopter;
parentless zombies are auto-reaped after their stacks are detached. `fork()`,
process groups, and user-controlled process creation remain unimplemented.

---

## Repository layout

```
charis__OS/
├── .github/workflows/ci.yml   # Build + 4-way boot matrix on every push
├── boot/
│   ├── boot.asm               # Multiboot2 entry, mode transitions
│   └── long_mode.asm          # 64-bit jump target
├── kernel/                    # C kernel source
│   ├── asm/                   # interrupt stubs, GDT, I/O, context switch
│   ├── main.c                 # kernel_main(), boot ordering, boot markers
│   ├── memory.c pmm.c vmm.c heap.c bootmem.c
│   ├── idt.c irq.c timer.c keyboard.c
│   ├── task.c scheduler.c syscall.c user.c
│   ├── vfs.c fs.c ata.c elf.c
│   ├── vga.c serial.c printf.c string.c
│   ├── net.c socket.c ipc.c pipe.c signal.c
│   ├── fb.c psf.c graphics.c compositor.c wm.c desktop.c widgets.c
│   ├── pci.c driver.c hda.c audio.c usb.c gamepad.c raster.c
│   └── config.c security.c services.c diagnostics.c display.c power.c
├── include/kernel/            # Kernel headers
├── iso/boot/grub/grub.cfg     # GRUB2 menu entry
├── sdk/                       # Game SDK headers
├── tools/verify-boot.sh       # Boot gate shared by CI and local runs
├── link.ld                    # GNU ld linker script
├── Makefile                   # Build system
├── build_wsl.sh / .bat        # WSL build helpers
├── .github/                   # CI
├── LICENSE                    # MIT
└── TODO.md                    # Authoritative project status
```

---

## Building

### Prerequisites

Debian / Ubuntu / Fedora / WSL:

```bash
# Debian / Ubuntu
sudo apt update
sudo apt install build-essential nasm grub-pc-bin grub-common xorriso qemu-system-x86

# Fedora
sudo dnf install gcc nasm grub2-tools xorriso qemu-system-x86
```

The build uses the **host** GCC with `-ffreestanding`; a cross-compiler is not
required. `nasm`, `grub-mkrescue`, and `xorriso` are all mandatory.

### Build and run

The kernel is compiled for the generic x86-64 baseline (`-march=x86-64 -mtune=generic -mno-bmi -mno-bmi2`). CI checks the linked kernel for accidental BMI/BMI2-family instructions so the default image does not silently require newer CPU extensions.

```bash
git clone https://github.com/Liladizilla/charis__OS.git
cd charis__OS

make                 # → build/charisos.iso  (~29 MB, includes GRUB)
make run             # boot the ISO in QEMU with serial output
```

Headless (useful for CI and debugging):

```bash
qemu-system-x86_64 -cdrom build/charisos.iso -m 256M -nographic
```

### Make targets

| Target | Effect |
|---|---|
| `make` | Build `build/charisos.iso` |
| `make images` | Build both named images (`charisos-vm.iso`, `charisos-usb.img`) |
| `make run` | Boot in QEMU (256MB, serial to stdout) |
| `make run-vm` | Boot the VM image as a CD-ROM |
| `make run-usb` | Boot the USB image as a raw disk (what a flashed stick looks like) |
| `make run-vm-uefi` / `make run-usb-uefi` | Same, booted with OVMF (UEFI) |
| `make verify-boot` | Boot gate — asserts the kernel reached the scheduler in every mode |
| `make debug` | QEMU with interrupt, `pcall` and MMU tracing → `qemu.log` |
| `make gdb` | QEMU paused on a GDB server at `:1234` |
| `make test` | Compile the VMM self-tests with `-DRUN_VMM_TESTS` |
| `make clean` | Remove `build/` and the staged kernel ELF |

---

## Boot images: VM vs bare metal

`make images` produces two named artifacts:

| File | Use |
|---|---|
| `build/charisos-vm.iso` | Attach as a **CD/DVD** in GNOME Boxes, virt-manager or QEMU |
| `build/charisos-usb.img` | `dd` to a **USB stick** and boot real hardware |

Both are produced by `grub-mkrescue`, which emits a *hybrid* image: an ISO9660
filesystem plus a protective MBR, a GPT header, and GRUB loaders for both
`i386-pc` (legacy BIOS) and `x86_64-efi` (UEFI). The two files are identical
bytes; they are named for their intended use. The image is verified to boot in
BIOS CD and raw-disk paths in CI. UEFI variants can be exercised with the\n`run-vm-uefi`, `run-usb-uefi` and `verify-boot` targets when compatible OVMF\nfirmware is installed; CI treats UEFI checks as advisory.

### Testing in a VM (GNOME Boxes / QEMU on Fedora)

Attach `charisos-vm.iso` as a CD-ROM. To exercise the filesystem, also attach a
blank disk formatted FAT32 — the kernel's ATA driver is legacy PIO and needs a
real device, it does not read the CD.

```bash
# Equivalent command line
qemu-system-x86_64 -cdrom build/charisos-vm.iso -drive file=fat32.img,format=raw,if=ide -m 256M
```

In GNOME Boxes: create a VM, add the ISO under *Optical Drive*, and optionally a
disk under *Additional drives*.

### Testing on bare metal

```bash
# Identify the whole-device node first — this destroys it
lsblk -d -o NAME,SIZE,MODEL,TRAN

sudo dd if=build/charisos-usb.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

Then reboot and pick the USB device from the firmware boot menu. Choose USB
first in the boot order if the machine also has an installed OS.

**Bare-metal caveats — read before testing.** The kernel is narrower than a
normal bootloader in ways that matter on real hardware:

- **Storage is legacy ATA PIO only** (ports `0x1F0`–`0x1F7`, primary channel).
  There is no AHCI or NVMe driver, so on a machine that only exposes AHCI there
  will be no filesystem and the kernel falls back to defaults. This is not a
  regression; it is simply unimplemented.
- **Networking is RTL8139 only** (PCI `10EC:8139`). Other NICs are ignored.
- **No USB support at all.** `usb.c` is a stub, so keyboards, mice and the USB
  stick itself are not driven by the kernel. A PS/2 keyboard and mouse are
  required for interactive use.
- **The firmware must hand off via GRUB2's `multiboot2` module.** Secure Boot
  will refuse the unsigned kernel unless GRUB's shim chain is trusted.
- The graphics stack drives a fixed 640×480 framebuffer; the Multiboot2 GFX
  tag is not yet parsed, so resolution is not negotiated with the firmware.

### Checking a boot without any of that

```bash
make verify-boot
```

This boots each image in QEMU and fails unless the kernel prints:

```
[BOOT] init complete, entering scheduler
```

That line is emitted on the serial console only after the Multiboot2 magic has
validated, every subsystem initialiser has returned, and the shell task has
been created. It is the only reliable health signal — the boot banner is
printed *before* the magic is validated, so a kernel that halts at the magic
check still prints it. The assertions live in
[`tools/verify-boot.sh`](./tools/verify-boot.sh), which CI runs too.

### Windows

```bat
build_wsl.bat
run.bat
```

---

## Boot-time debugging

If the kernel hangs, the markers in the VGA text buffer tell you where. For
example, seeing `S12344567[L>` means serial, memory, IDT, IRQ, timer, keyboard,
ATA, FS, VFS, tasks, scheduler, syscalls, graphics, WM, and config all returned.

To get serial output in a file for inspection:

```bash
qemu-system-x86_64 -cdrom build/charisos.iso -m 256M -nographic -serial file:serial.log
```

Combine with `make gdb` and:

```
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

---

## Security

Capability bits are defined in `include/kernel/security.h` and sanitized against
the kernel's defined-bit mask when a task is constructed. Capability checks use
conjunctive semantics: a request containing multiple bits succeeds only when
every requested bit is held. Empty requests and requests containing undefined
bits fail closed. The `fork` syscall is not implemented and currently returns
failure; child capability inheritance is therefore not claimed.

| Authority | Enforcement |
|---|---|
| `CAP_FS_READ` | Required to request and rechecked for non-console reads |
| `CAP_FS_WRITE` | Required to request a write-capable descriptor and rechecked for non-console writes |
| `FD_RIGHT_READ` | Per-descriptor grant checked by `sys_read` and the ELF loader |
| `FD_RIGHT_WRITE` | Per-descriptor grant checked by `sys_write` |
| `CAP_SPAWN` + `CAP_ALLOW_EXEC` | `sys_exec` authorization |
| `CAP_NET_RAW`, `CAP_SYS_ADMIN`, `CAP_RAW_MEM`, `CAP_SHUTDOWN` | Reserved |

The built-in network worker, text shell and graphical desktop worker are
kernel-mode tasks. Their current call paths use trusted in-kernel APIs and do
not invoke capability-gated filesystem or process syscalls, so they receive an
empty process capability mask. This is least privilege at the syscall boundary,
not a claim that ring-0 code is sandboxed from kernel internals. The bootstrap
ring-3 task retains `CAP_FS_READ | CAP_SPAWN` for read-policy and execution-denial
regression coverage, but intentionally does not receive `CAP_ALLOW_EXEC`.

`sys_open(path)` remains read-only by default. `sys_open_rights(path, rights)`
can request an explicit read/write combination, but the process must hold the
matching filesystem capabilities and the VFS node must implement each requested
operation. The descriptor stores only the granted rights; later reads and writes
must pass both the descriptor check and the underlying operation check.
Standard input receives read authority, while standard output/error receive
write authority. Closing a descriptor revokes its table entry before the
filesystem close callback can run. Each successful open creates a kernel-owned
`vfs_open_file_t` containing the node, shared offset, open flags, I/O lock and
packed reference lifecycle. The task-local `fd_entry_t` stores only the open-file
pointer, granted rights and handle generation. `fd_acquire()` validates the
current task and required rights while holding the descriptor-table lock, then
pins the open-file object for the complete operation. `fd_release()` drops that
pin; the final release atomically enters a permanent closing state before
calling the node destructor exactly once. A reference cannot be acquired from
zero or from a closing object.

A global atomic node-ownership registry still ensures that one closeable raw
node belongs to exactly one open-file description. It prevents two independent
open descriptions from claiming one node pointer, including simultaneous
allocations on separate CPUs. A losing opener receives an explicit already-owned
status and must not run the other description's close callback. During final
close, the registry marks the pointer as releasing; a new claim for a reused
address waits until the old callback returns. The registry uses integer pointer
keys so it never dereferences a node after its callback may have freed it.
The per-open-file I/O lock serializes VFS callbacks and shared-offset updates.
Callbacks under that lock must remain bounded and must not block or yield;
callback execution and object destruction happen outside the descriptor-table
lock. Standard streams bind to permanent shared kernel open-file objects.

The kernel-internal `fd_duplicate_to()` primitive can publish a descriptor
for a live recipient PID. It rejects zero/unknown rights and any requested
right the sender does not hold, checks recipient state and table capacity,
acquires the shared open-file reference before publication, and returns a
recipient-bound task-scoped handle. The sender and recipient retain independent
rights while sharing the same open-file offset. Task exit publishes
`TASK_STATE_EXITING` before its close-all pass, preventing a new duplicate
from appearing after cleanup. Host regressions cover rights escalation,
full-table rollback, sender close, recipient cleanup and rejected teardown
states. No ring-3 syscall exposes this primitive; userspace descriptor transfer
still requires a separately designed, authenticated IPC protocol.

The global integer-channel IPC API has been removed. The kernel now creates
explicit endpoint pairs and installs generation-protected handles in each
owner task's local endpoint table. Handles resolve only for the current task;
SEND and RECEIVE rights are checked independently, and the peer PID/pointer is
established by the kernel rather than supplied by a message. Each direction has
a bounded 16-message queue with a 512-byte message limit. Send is nonblocking;
queue-full, empty, undersized-buffer and peer-closed outcomes are explicit, and
messages are never silently truncated. A receiver drains already queued
messages and then receives PEER_CLOSED when its peer closes. Closing an endpoint
revokes its handle, discards its own inbox and releases one channel reference. Task exit closes all endpoints
immediately after publishing EXITING.

This is a kernel-only first stage: endpoint-pair creation rejects ring-3 tasks,
syscall slots 91-93 are unregistered, and the legacy user wrappers are removed.
Kernel-only blocking receive now uses atomic waiter registration and teardown
cancellation. QEMU verifies message wake, peer task exit, and revocation of a
blocked reader's local endpoint, including CANCELLED/PEER_CLOSED results and
worker reaping. Kernel-only named service rendezvous supports CAP_IPC_SERVICE
registration, client-capability policy, blocking connect/accept, explicit
rejection and bounded pending requests. Scheduler-backed QEMU coverage verifies
a blocked server accept, blocked client connect, connection wakeup, endpoint
rights attenuation, request rejection and unregister waking a blocked client.
Ring-3 service lookup, descriptor attachments and transactional receive rollback
remain unimplemented. Shared-memory syscalls also remain kernel-only because
the current API returns raw kernel pointers and has no per-task mapping/revocation
model. The endpoint, rendezvous and resource-transfer contract is in
[Authenticated IPC Endpoints and Resource Transfer](./docs/architecture/authenticated-ipc-endpoints-and-resource-transfer.md).
These are explicit security boundaries, not claims that public IPC is complete.
Descriptor tables are task-local and are rebuilt when a task slot is reused. Non-standard descriptor values are 64-bit task-scoped
identifiers whose ABI encoding callers must treat as opaque. The current
encoding is deterministic: it contains the owning task PID, a per-slot
generation and the descriptor slot. It is not random, secret, MAC-protected or
unforgeable, and it is not a security credential or an intra-task isolation
boundary. Its guarantees are narrower and explicit: a closed handle cannot
resolve after slot reuse, and a handle copied into another task cannot resolve
there even when the same slot is occupied. Task PIDs are allocated with atomic
compare/exchange, remain monotonic, and fail closed rather than wrapping, so
identities are unique even when task creation races across CPUs and are never
recycled. The generation counter is preserved across task-slot reuse and also
fails closed rather than wrapping. `FD_HANDLE_INVALID` uses owner PID zero, which the monotonic PID
allocator never issues. This leaves every nonzero-PID descriptor tuple
available, including the all-ones 64-bit handle at the maximum PID, generation
and slot values. Standard input/output/error retain
the stable handles 0, 1 and 2. `sys_open()` and `sys_open_rights()` return
`FD_HANDLE_INVALID` (owner PID zero) on failure; callers must not interpret
zero or an arbitrary negative integer as a failed non-standard handle. Numeric
handles are not transferable capabilities; future cross-task delegation
requires a separate, rights-attenuating resource-transfer mechanism.

Kernel-side file consumers use the VFS node's `close` callback rather than
assuming a particular allocator. The IL bytecode loader validates the path,
requires a kernel caller, rejects files larger than its fixed instruction
array or files ending mid-instruction, requires a complete read, and releases
the per-open node on both success and failure. The Browser's local-file preview
and ELF loader's pre-descriptor validation path also release resolved nodes
through their VFS close callback.
VFS callback return values are bounded by each caller: ELF exact reads reject
over-reported byte counts and wrapped offsets, configuration loading rejects
oversized results before placing the string terminator, and Browser previews
reject lengths larger than their destination buffer. Descriptor read/write and
ELF read ranges fail closed before 64-bit offset arithmetic can wrap.

The FAT32 VFS adapters also reject offsets above the 32-bit `file_t.pos`
domain before narrowing a 64-bit VFS offset or entering the filesystem layer.
A host regression verifies that oversized reads do not invoke `fs_read`, and
oversized writes do not invoke `fs_open` or `fs_write`.

Before a ring-3 `sys_read` touches its input source, the kernel validates the
entire writable destination range across page boundaries, using effective
USER/WRITABLE page-table permissions and checking that each physical frame is
reachable through the kernel direct map. An unmapped, read-only or overflowing
range fails before keyboard input or a VFS read callback can consume data.
Copy-out repeats whole-range validation before modifying userspace. This is a
specific `SYS_READ` ordering guarantee, not a claim that all syscall pointer
paths are complete.

`security_verify_path()` rejects parent-directory traversal and alternate
separators. When RDRAND is available, it can seed the stack guard. If RDRAND is
unavailable, cryptographic token generation is disabled. The kernel does not
claim that a weak fallback provides cryptographic entropy.

**Known gaps:** per-open rights are not yet transferable or inherited, and the
broader resource model is incomplete. Capability bits are still assigned by
kernel task constructors; there is no user-controlled delegation, attenuation
API or per-resource capability object yet. Task-owned IPC endpoints, sockets,
networking, diagnostics and shared-memory interfaces remain kernel-only rather
than being exposed to ring 3. Full process isolation and broader capability
enforcement remain unfinished.

See [`SECURITY.md`](./SECURITY.md) for reporting guidance.

---

## Roadmap

The immediate priority is a trustworthy, consistent native desktop rather than
a large collection of unfinished applications.

1. Repository and documentation accuracy, repeatable builds and reviewable
   releases.
2. Shared UI foundations: measured layout, word wrapping, clipping, scrolling,
   font metrics and resolution/scale regression tests.
3. A real desktop shell and consistent multi-window lifecycle.
4. Complete the first core applications: Files, Terminal, Settings, Text Editor
   and System Monitor.
5. Continue kernel security and stability work: capability enforcement,
   process reaping, ELF/address-space lifecycle, malformed-input testing and
   long-session regression.
6. Expand real hardware support, writable storage, networking and installation.

The exact Working / Partial / Planned status is maintained in [`TODO.md`](./TODO.md).
The design rules and current UI limitations are documented in
[`docs/charis-design-language.md`](./docs/charis-design-language.md).

---

## References

| Resource | Topic |
|---|---|
| [OSDev Wiki](https://wiki.osdev.org) | x86 hardware reference |
| [The Little OS Book](https://littleosbook.github.io) | OS foundations |
| [James Molloy's Kernel Tutorial](https://jamesmolloy.co.uk) | C kernel walkthrough |
| [Multiboot2 spec](https://www.gnu.org/software/grub/manual/multiboot2/multiboot2.html) | Boot protocol |
| [Beej's Guide to Network Programming](https://beej.us/guide/bgnet) | Sockets |
| [Writing a Simple TCP/IP Stack](https://saminiir.com/lets-code-tcp-ip-stack) | Network stack |
| [Dan Luu — malloc tutorial](https://danluu.com/malloc-tutorial) | Allocator design |
| [Crafting Interpreters](https://craftinginterpreters.com) | Bytecode VMs |

---

## Contributing

1. Fork the repository
2. Create a branch: `git checkout -b feat/your-feature`
3. Commit: `git commit -m "feat: describe your change"`
4. Push and open a Pull Request

Please run `make` and boot the ISO in QEMU before opening a PR. CI does this
automatically on every push and will reject a change that does not build or
boot. Read [`SECURITY.md`](./SECURITY.md) before reporting security issues.

---

## Author

**Charis Chara**

- GitHub: [@Liladizilla](https://github.com/Liladizilla)
- Portfolio: [portfolio-self-five-47.vercel.app](https://portfolio-self-five-47.vercel.app)
- Personal site: [cyberzilla01.pp.ua](https://cyberzilla01.pp.ua)

---

## License

MIT — see [`LICENSE`](./LICENSE).

---

<div align="center">
  <sub>An independent x86_64 operating system built from scratch in C and NASM.</sub>
</div>
