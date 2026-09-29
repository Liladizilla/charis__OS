<div align="center">

# ⚙️ CharisOS

**A from-scratch x86_64 operating system kernel — written in C & NASM, with no libc and no host OS**

![Arch](https://img.shields.io/badge/arch-x86__64-blue?style=flat-square)
![Lang](https://img.shields.io/badge/lang-C%20%2B%20NASM-purple?style=flat-square)
![Boot](https://img.shields.io/badge/boot-Multiboot2%20%2F%20GRUB2-orange?style=flat-square)
![CI](https://github.com/Liladizilla/charis__OS/actions/workflows/ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)
![Stars](https://img.shields.io/github/stars/Liladizilla/charis__OS?style=flat-square&color=yellow)

*Built and maintained by **Charis Chara** · Nairobi, Kenya*

</div>

```
╔═════════════════════════════════════════════════════╗
║    ████ █   █  ███  ████  █████  ████  ███   ████   ║
║   █     █   █ █   █ █   █   █   █     █   █ █       ║
║   █     █   █ █   █ █   █   █   █     █   █ █       ║
║   █     █████ █████ ████    █    ███  █   █  ███    ║
║   █     █   █ █   █ █ █     █       █ █   █     █   ║
║   █     █   █ █   █ █  █    █       █ █   █     █   ║
║    ████ █   █ █   █ █   █ █████ ████   ███  ████    ║
║                                                      ║
║  x86_64 kernel  //  built from scratch in C & NASM   ║
║               v1.0  -  Nairobi, Kenya                ║
╚═════════════════════════════════════════════════════╝
```

---

## 📽️ Demo

- [Boot demo — part 1](https://file.kiwi/67a1f9ef#LIVXxNmocS8L9P13rQx1CQ)
- [Boot demo — part 2](https://file.kiwi/7d8484fd#TjQmPCrzKGcYy7rqvSBifQ)

---

## 📖 About

CharisOS is an educational, from-scratch x86_64 kernel. Everything below the
GRUB handoff is written by hand: the bootloader assembly, the physical and
virtual memory managers, the interrupt tables, the preemptive scheduler, the
SYSCALL/SYSRET ABI, the VFS with a FAT32 backend, an ELF program loader, and a
framebuffer graphics/window-manager stack.

There is no standard library, no runtime, and no host-OS dependency. The kernel
compiles with `gcc -ffreestanding` and links with a hand-written `link.ld`.

**Scale:** ~9,100 lines of C across 107 files, ~720 lines of NASM across 6 files.

### Size, and what "real OS" means

The full image is **30 MB** and the kernel inside it is **151 KB**. The rest is
the bootloader: `grub-mkrescue` embeds 578 GRUB modules (290 for BIOS, 288 for
UEFI) plus 86 locale files, almost none of which this kernel uses. The OS is
0.5% of the image.

That is worth saying plainly, because "make it a gigabyte" is the wrong goal.
A distribution image is large when it contains real things: a C library, a
compiler, a window manager, a browser engine, thousands of binaries, fonts,
documentation and translations. Padding the ISO to a gigabyte with data nothing
executes would make the number bigger and the system no more capable.

The honest route to a substantial system is to build the subsystems, and the
size follows from them. The order below is by dependency, not by appeal:

| # | Work | Unblocks |
|---|---|---|
| 1 | **Fix `task_exit_handler`** — it `kfree()`s the stack it is executing on | Any process exit |
| 2 | **Writable VFS** (`create`/`mkdir` in `vfs_node_t`) | Setup surviving a reboot, any app that saves |
| 3 | **Ring-3 scheduling** — CR3 on first switch, `TSS.rsp0`, `address_space`, user stack mapped | Userspace, isolation, real processes |
| 4 | **A libc** in C and assembly: `mem*`, `str*`, `printf`, syscalls | Every future program |
| 5 | **Real `fork`/`exec`/`wait`** | Multitasking, a shell that runs programs |
| 6 | **A build system for userland** | Compiling programs at all |
| 7 | **Drivers: AHCI, USB HID, more NICs** | Using a real modern machine |
| 8 | **Applications** | Anything to be "a real OS" for |

Items 3 and 4 are the gate. Nothing that resembles a general-purpose OS exists
until a process can run outside the kernel and call into a library.

### Project status

This is an active educational kernel. It boots in QEMU and reaches an
interactive shell, verified in CI on every push. Several subsystems are
complete and exercised; others are present but partial.
[`TODO.md`](./TODO.md) is the authoritative status document and is kept honest
— a phase is only marked complete once it has been booted and exercised, not
merely compiled.

| Area | Status |
|---|---|
| Boot (Multiboot2 → long mode), BIOS and UEFI | ✅ Working |
| Graphics on BIOS (framebuffer request in the MB2 header) | ✅ Working |
| Physical + virtual memory, heap | ✅ Working |
| IDT/IRQ, PIT timer, PS/2 keyboard, serial | ✅ Working |
| Preemptive round-robin scheduler, task stacks | ✅ Working (kernel-mode tasks) |
| Interactive shell | ✅ Working |
| SYSCALL/SYSRET ABI + syscall table | ✅ Working |
| VFS + FAT32 (read path) | ✅ Working |
| ELF loader (`sys_exec`) | ⚠️ Loads, but cannot enter ring 3 yet |
| **Ring-3 userspace** | ❌ **Not implemented — the gate on everything below** |
| **libc / userland build** | ❌ Not started |
| Framebuffer, window manager, desktop | ⚠️ Partial |
| Capability-based security | ⚠️ Partial — enforced on `open`/`exec` only |
| Config persistence (`/etc/charisos.conf`) | ⚠️ Read path works; no file creation yet |
| RTL8139 networking | ⚠️ TX wired; no TCP/IP or RX |
| USB, audio, gamepad | ⚠️ Stubs |

> **Ring 3 is not implemented.** The kernel boots and runs its shell, but only
> because the ring-3 "user" task is deliberately not enqueued. Four gaps remain
> — CR3 is never loaded on the first switch, `TSS.rsp0` is never set, the
> scheduler switches on a field the setup path never assigns, and the user
> stack is unmapped in the new address space. Enqueuing the user task today
> makes the machine triple-fault on the first timer tick.
> [`TODO.md`](./TODO.md) tracks each one.

---

## ✨ Features

| Category | Details |
|---|---|
| **Boot** | Multiboot2 via GRUB2 · protected → long mode · CPUID feature checks · identity paging with 2MB huge pages · 16KB boot stack |
| **Memory** | Bitmap PMM (next-fit, double-free guarded) · split/merge heap (`kmalloc`/`kfree`) · per-process PML4 address spaces |
| **Scheduling** | Preemptive round-robin on a 1kHz PIT tick · 32 tasks · 8KB stacks · guard pages + stack canaries |
| **Interrupts** | 256-entry IDT · PIC remapped to IRQ 32–47 · page-fault and exception handlers with diagnostics |
| **Syscalls** | `SYSCALL`/`SYSRET` via MSR (LSTAR/STAR/SFMASK) · 34 registered syscalls |
| **Security** | Capability bitmask per task, inherited across `fork` · stack canaries · path-traversal rejection · enforced on `open`/`exec` |
| **VFS** | Node abstraction with read/write/open/close/readdir/finddir function pointers · device nodes · per-task fd tables |
| **Storage** | ATA PIO driver · FAT32 read path |
| **Loader** | `sys_exec` — parses ELF64 headers, maps `PT_LOAD` segments, builds an initial stack |
| **Graphics** | Framebuffer driver · PSF2 bitmap font renderer · 2D primitives (line/rect/circle) · window manager with drag, focus and z-order · desktop with taskbar |
| **Drivers** | VGA text · PS/2 keyboard and mouse · PIT · PCI enumeration with a probe/remove driver framework · HDA audio · RTL8139 Ethernet |
| **Shell** | Built-in interpreter: `help`, `ls`, `echo`, `net`, `uptime`, `clear` |

---

## 🏗️ Architecture

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
   ├─ builds PML4 → PDPT → PD, identity-maps 1GB with 2MB pages
   ├─ enables PAE + LME + NX + PG, loads GDT + TSS
   ▼
boot/long_mode.asm     64-bit
   ├─ loads segment selectors, clears registers
   ├─ rsp ← stack_top
   └─ call kernel_main(mb_magic, mb_info)
   ▼
kernel/main.c :: kernel_main()
   ├─ vga_init, serial_init, Multiboot2 magic validation
   ├─ memory_init → idt_init → irq_init → timer_init(1000) → keyboard_init
   ├─ ata_init → fs_init → vfs_init
   ├─ task_init → scheduler_init → syscall_init
   ├─ graphics_init → wm_init → input_init → ipc_init → audio_init
   ├─ signal_init → pipe_init → driver_init → pci_scan → driver_scan_and_bind
   ├─ config_init → config_load("/etc/charisos.conf")
   ├─ display_init → desktop_init → services_init → diag_init
   ├─ power_init → security_init
   ├─ net_init  (gated on config "net_enabled")
   ├─ create "user" and "shell" tasks
   └─ sti; scheduler_start()  →  shell_main()
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

## 📁 Repository layout

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

## 🛠️ Building

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

## 💾 Boot images: VM vs bare metal

`make images` produces two named artifacts:

| File | Use |
|---|---|
| `build/charisos-vm.iso` | Attach as a **CD/DVD** in GNOME Boxes, virt-manager or QEMU |
| `build/charisos-usb.img` | `dd` to a **USB stick** and boot real hardware |

Both are produced by `grub-mkrescue`, which emits a *hybrid* image: an ISO9660
filesystem plus a protective MBR, a GPT header, and GRUB loaders for both
`i386-pc` (legacy BIOS) and `x86_64-efi` (UEFI). The two files are identical
bytes; they are named for their intended use. The image is verified to boot in
all four combinations — CD and raw disk, BIOS and UEFI — by `make verify-boot`.

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

## 🔭 Boot-time debugging

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

## 🔒 Security

Capability bits are defined in `include/kernel/security.h` and assigned per task
at creation; children inherit the parent's set across `fork`.

| Capability | Guards |
|---|---|
| `CAP_FS_READ` | `sys_open` |
| `CAP_SPAWN` | `sys_exec`, `sys_fork` |
| `CAP_NET_RAW`, `CAP_SYS_ADMIN`, `CAP_RAW_MEM`, `CAP_SHUTDOWN` | reserved |

`security_verify_path()` rejects `..` traversal. Stack canaries are seeded from
an RDTSC/timer entropy mix in `security_init()`.

**Known gaps:** enforcement currently covers `open` and `exec` only — the VFS
read/write paths, socket syscalls, and shared memory are unchecked. The
capability set is not persisted or namespaced per-user.

See [`SECURITY.md`](./SECURITY.md) for reporting guidance.

---

## 🗺️ Roadmap

Near-term work, in rough priority order:

- [ ] TCP/IP stack on the RTL8139 driver (ARP, ICMP, UDP, TCP) + RX path
- [ ] `wait()` / `waitpid()` syscalls and zombie reaping
- [ ] Wire the Settings app to `config_set_*` + `config_save()`
- [ ] Add `mkdir`/`create` to the VFS so `/etc/charisos.conf` can be created
- [ ] Enforce capabilities on VFS read/write, sockets and shared memory
- [ ] Add `-Werror` to the build to prevent regression
- [ ] Run `vmm_run_tests()` at boot behind a build flag
- [ ] Userspace: real Ring 3 processes — CR3 load on first switch, `TSS.rsp0`, wire `address_space`, map a real user stack. Detailed in [`TODO.md`](./TODO.md).

[`TODO.md`](./TODO.md) tracks all of this in detail.

---

## 📚 References

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

## 🤝 Contributing

1. Fork the repository
2. Create a branch: `git checkout -b feat/your-feature`
3. Commit: `git commit -m "feat: describe your change"`
4. Push and open a Pull Request

Please run `make` and boot the ISO in QEMU before opening a PR. CI does this
automatically on every push and will reject a change that does not build or
boot. Read [`SECURITY.md`](./SECURITY.md) before reporting security issues.

---

## 👤 Author

**Charis Chara**

- GitHub: [@Liladizilla](https://github.com/Liladizilla)
- Portfolio: [portfolio-self-five-47.vercel.app](https://portfolio-self-five-47.vercel.app)
- Personal site: [cyberzilla01.pp.ua](https://cyberzilla01.pp.ua)

---

## 📄 License

MIT — see [`LICENSE`](./LICENSE).

---

<div align="center">
  <sub>Written from scratch — no OS, no stdlib, no shortcuts.</sub>
</div>
