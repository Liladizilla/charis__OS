# CharisOS

**An experimental x86_64 operating-system project built from the ground up in freestanding C and NASM.**

[![CI](https://github.com/Liladizilla/charis__OS/actions/workflows/ci.yml/badge.svg)](https://github.com/Liladizilla/charis__OS/actions/workflows/ci.yml)
![Architecture](https://img.shields.io/badge/architecture-x86__64-informational)
![Languages](https://img.shields.io/badge/kernel-C%20%2B%20NASM-informational)
![Boot](https://img.shields.io/badge/boot-GRUB2%20%2F%20Multiboot2-informational)
![License](https://img.shields.io/badge/license-MIT-informational)

Maintained by **Charis Chara** · [Source repository](https://github.com/Liladizilla/charis__OS)

---

## Project overview

CharisOS is a from-scratch operating-system research project targeting x86_64 PCs and virtual machines. The repository contains a freestanding kernel, Multiboot2/GRUB boot path, physical and virtual memory management, interrupt and task infrastructure, a small syscall interface, FAT32-backed storage support, a static ELF executable loader, and a framebuffer-based graphical shell.

The project is under active development. It is not currently a general-purpose or production-ready operating system, and it should not be treated as a replacement for Windows or Linux. This README distinguishes implemented and tested behavior from partial subsystems and planned work.

**Implementation constraints:** kernel and boot implementation use C and NASM. The kernel is built freestanding with GCC and a custom linker script; it does not depend on a host operating-system runtime after boot.

## Current state at a glance

| Subsystem | Current evidence-based status |
|---|---|
| BIOS boot | CI verifies GRUB/Multiboot2 boot from CD image and raw disk image in QEMU |
| UEFI boot | UEFI paths exist; CI coverage is advisory and firmware-dependent |
| CPU and memory | x86_64 long-mode entry, PMM, VMM, heap and page-permission regression tests |
| Interrupts and scheduling | IDT/PIC/PIT, PS/2 input and kernel-task scheduling; ring-3 yield/resume exercised in QEMU |
| User mode | DPL3 `int 0x80` path exercised; static x86_64 `ET_EXEC` smoke program loaded from FAT32 and entered in ring 3 |
| Storage | Primary ATA PIO and FAT32 support; current Files interface is read-only and limited to the root directory and 8.3 names |
| Graphical shell | Framebuffer workspace with independent Terminal, Files, Browser, Settings and About windows; workspace and application coverage remain limited |
| Window manager | Bounded ownership, back-to-front composition, focus-only input, move, lower-right resize grip, maximize/restore, minimize, taskbar switching and Alt+Tab; snap/tiling and QEMU pointer-resize validation remain unfinished |
| Settings | Font family, text size and interface scale; CI verifies saved values survive a reboot on a writable FAT32 test disk |
| Security | Capability checks and input validation exist in selected paths; enforcement and isolation are incomplete |
| Networking | RTL8139 transmit path only; receive and TCP/IP stack are not implemented |
| USB and peripherals | USB and gamepad support are stubs; audio support is incomplete |
| Userspace | No complete libc, package manager, installer or general-purpose userspace environment |

For the authoritative subsystem-by-subsystem roadmap, see [TODO.md](./TODO.md). A subsystem is not considered complete merely because it compiles.

## Desktop screenshots

The checked-in gallery remains a historical overview. The latest successful QEMU desktop evidence for the workspace hierarchy increment is attached to [CI run #43025957282](https://github.com/Liladizilla/charis__OS/actions/runs/43025957282). Download its `charisos-desktop-preview` artifact for 11 captures covering the workspace, Browser, Files, simultaneous Files/Terminal windows, maximized and resized Terminal, default/adjusted/reloaded Settings, and the scaled desktop. The `boot-logs` and `charisos-images` artifacts provide the corresponding serial/QEMU logs and build outputs. The follow-up label/unit cleanup on PR #109 is being revalidated in [run #43027374029](https://github.com/Liladizilla/charis__OS/actions/runs/43027374029); do not treat that run as passed until GitHub reports success.

![CharisOS desktop, multi-window, maximized Terminal and Settings captures](docs/screenshots/charisos-preview-gallery.jpg)

## Latest desktop validation

The latest successful full desktop validation is [run #43025957282](https://github.com/Liladizilla/charis__OS/actions/runs/43025957282), on commit `d8688b2`. Its 19-stage workflow passed the kernel build with `-Werror`, generic x86-64 baseline audit, browser extraction tests, WM lifecycle/geometry/focus/maximize tests, ELF image construction and inspection, BIOS/ring-3/ELF execution, VMM huge-page and page-permission runtime tests, QEMU desktop capture, Settings persistence/reboot verification and artifact publication. Commit `f39cc3c` subsequently corrected the displayed memory units and removed a duplicate include; its CI rerun is tracked separately above.

The QEMU captures cover:
- The workspace heading, four labelled Files/Terminal/Settings/Browser launch cards, and System Overview with uptime plus PMM free/managed/allocated memory counters.
- Browser, Files, simultaneous Files/Terminal windows, maximized and resized Terminal, and default/adjusted/reloaded Settings.
- Alt+Tab focus routing, Terminal's built-in `help` command, Super+Up maximize, Super+Down restore, and Alt+F8 keyboard resize with arrow keys, Enter to commit and Escape to cancel.
- A reboot round-trip confirming `font_scale=2` and `ui_scale=120` reload from `/CHARISOS.CFG`.

These are automated engineering captures, not release marketing images. The workspace now has a labelled four-app launch grid and live kernel status counters, but it is still an early shell rather than a release-ready desktop. The scaled capture exposes typography/layout collisions at larger text sizes; responsive layout and multi-resolution verification remain open. Files is a read-only root browser, the shell and app renderers are still kernel-owned, and pointer-driven resize has not yet been exercised by QEMU automation.

## What is implemented

- GRUB2 and Multiboot2 boot path into x86_64 long mode.
- Physical memory bitmap allocator, virtual memory management and kernel heap.
- IDT, PIC, PIT, PS/2 keyboard/mouse and diagnostic output.
- Kernel-task scheduling and a tested ring-3 bootstrap path.
- DPL3 `int 0x80` syscall dispatch and a static ELF64 `ET_EXEC` smoke path.
- VFS abstractions with a limited FAT32 backend over primary ATA PIO.
- Framebuffer drawing and PSF2 bitmap-font rendering.
- Native graphical workspace, launcher/taskbar, Terminal, Files, Browser, Settings and About. Each application is registered as an independent bufferless WM surface and painted inside its own clipped window rectangle.
- Window-manager back-to-front composition, per-window focus and keyboard/pointer dispatch, titlebar dragging, a lower-right pointer resize grip, Alt+F8 keyboard resizing, maximize/restore, taskbar focus/minimize/restore, Alt+Tab cycling and visible focus rules. Host lifecycle, geometry, clipping, event-routing and allocation-failure tests cover the foundation; QEMU verifies simultaneous windows and keyboard maximize/restore.
- Persistent display preferences on supported writable FAT32 storage.
- CI boot, memory-permission, ring-3, ELF, desktop-capture and host-side WM lifecycle/bounds checks.

## Important limitations

CharisOS is still an early-stage kernel and shell. In particular:

- The desktop now supports independent Terminal, Files, Browser, Settings and About windows with z-order composition, per-window focus/input, titlebar dragging/resizing, keyboard resize, maximize/restore, taskbar switching and Alt+Tab. Snap/tiling behavior, dirty-region repaint and a richer workspace are still unfinished; this is a small kernel-owned shell, not a mature desktop environment.
- The workspace exposes direct shortcuts for Files, Terminal, Settings and Browser; the separate multi-window capture demonstrates Files and Terminal open together. Files is read-only, Settings currently exposes display preferences, and the desktop is not yet at release-quality visual or interaction completeness.
- Files does not yet support writable files, nested directory navigation or long filenames.
- The executable loader currently targets static ELF64 `ET_EXEC`; dynamic linking and a complete process lifecycle are unfinished.
- The `SYSCALL/SYSRET` user-mode path is configured but not yet validated as the normal syscall entry.
- Capability enforcement covers only selected operations. It is not a complete security boundary.
- The current entropy source is hardware-feature-dependent. If RDRAND is unavailable, cryptographic token generation is disabled; no weak fallback is presented as cryptographic randomness.
- Storage support is limited to legacy primary-channel ATA PIO and FAT32. AHCI and NVMe are not implemented.
- Networking is limited to an RTL8139 transmit path. TCP/IP and receive support are not implemented.
- USB support is not implemented, and audio/peripheral support remains partial.
- The graphics path uses the currently supported framebuffer mode; broad hardware and resolution coverage remains future work.

Do not use this project to store sensitive data or as the only operating system on a machine.

## Documentation map

| Document | Purpose |
|---|---|
| [TODO.md](./TODO.md) | Current implementation status, milestones and remaining work |
| [docs/charis-design-language.md](./docs/charis-design-language.md) | Desktop visual principles, scaling goals and current UI limitations |
| [SECURITY.md](./SECURITY.md) | Security reporting and responsible-disclosure guidance |
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
`CLOSE`=3, `GETPID`=39, `FORK`=57, `EXEC`=59, `EXIT`=60) and uses a
CharisOS-specific band above 70 for sockets, IPC, diagnostics and the game SDK.
This is a custom ABI — it is **not** Linux-ABI compatible.

### Scheduling

The ready queue is a singly-linked list. `scheduler_find_next()` walks it and
selects the first `TASK_STATE_READY` task, so blocked tasks stay in place and are
skipped rather than dequeued. Simple and correct for the current 32-task
ceiling, but every tick is O(n) over all tasks, so it would need a real priority
structure before the task limit is raised.

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

Capability bits are defined in `include/kernel/security.h` and assigned per task
at creation; children inherit the parent's set across `fork`.

| Capability | Guards |
|---|---|
| `CAP_FS_READ` | `sys_open` |
| `CAP_SPAWN` | `sys_exec`, `sys_fork` |
| `CAP_NET_RAW`, `CAP_SYS_ADMIN`, `CAP_RAW_MEM`, `CAP_SHUTDOWN` | reserved |

`security_verify_path()` rejects parent-directory traversal. When RDRAND is available, it can seed the stack guard. If RDRAND is
unavailable, cryptographic token generation is disabled. The kernel does not
claim that a weak fallback provides cryptographic entropy.

**Known gaps:** enforcement currently covers `open` and `exec` only — the VFS
read/write paths, socket syscalls, and shared memory are unchecked. The
capability set is not persisted or namespaced per-user.

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
