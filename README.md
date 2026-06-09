<div align="center">

# ⚙️ CharisOS

**A handcrafted x86_64 operating system kernel — built from scratch in C & NASM Assembly**

![Architecture](https://img.shields.io/badge/Architecture-x86__64-purple?style=flat-square)
![Language](https://img.shields.io/badge/Language-C%20%2F%20NASM-blue?style=flat-square)
![Boot](https://img.shields.io/badge/Boot-Multiboot2%20%2F%20GRUB2-orange?style=flat-square)
![Tested On](https://img.shields.io/badge/Tested%20On-QEMU%20%2B%20Bare%20Metal-green?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-gray?style=flat-square)
![Stars](https://img.shields.io/github/stars/Liladizilla/charis__OS?style=flat-square&color=yellow)

*Built and maintained by **Charis Chara** · Nairobi, Kenya*

</div>

---

## 📽️ Demo

<!-- Replace the link below with your actual video URL and thumbnail -->
[![CharisOS Boot Demo](file:///C:/Users/chara/Videos/charis_os_presentation.mp4)

> **Adding your video:** Upload your recording to YouTube or a file host, then replace the badge href above with your URL. For a thumbnail preview, use:
> ```md
> [![Demo](assets/demo-thumb.png)](https://youtu.be/YOUR_VIDEO_ID)
> ```

---

## 📖 About

CharisOS is a lightweight, educational operating system for the **x86_64 architecture**. It combines low-level Assembly boot logic with a minimal C runtime, implementing a managed-style kernel that emphasizes simplicity, security, and bare-metal performance — with zero dependency on a host OS or standard library.

This project covers the full journey from BIOS handoff to preemptive multitasking, interrupt handling, virtual memory, device drivers, a VFS with FAT32, ELF binary loading, a framebuffer graphics stack, and a CIL bytecode interpreter — all written by hand.

---

## ✨ Features

| Category | Details |
|---|---|
| **Boot** | Multiboot2/GRUB2 · Real → Protected → Long mode · 2MB huge pages · 16KB kernel stack |
| **Memory** | Bitmap PMM · Best-fit heap allocator (split/merge) · VMM per-process page tables |
| **Scheduling** | Preemptive round-robin · Spinlock-protected queues · 32 tasks × 8KB stacks |
| **Security** | Stack canaries · Guard pages · Capability-based access (CAP_FS_READ, CAP_SPAWN …) |
| **Syscalls** | SYSCALL/SYSRET dispatch · MSR setup · Ring 0/3 separation |
| **VFS** | Full VFS layer · FAT32 driver · Device nodes |
| **ELF Loader** | `sys_exec()` — loads and runs ELF binaries |
| **Graphics** | Framebuffer stack · PSF2 bitmap fonts · Window manager (`wm.c` / `desktop.c`) |
| **Drivers** | VGA 80×25 text · PS/2 keyboard · PIT 1000Hz · RTL8139 Ethernet |
| **Shell** | Built-in interpreter — `help`, `ls`, `echo`, `net`, `uptime`, color-coded output |
| **IL Runtime** | CIL/IL bytecode interpreter (`il_runtime.c`) — managed code foundation |
| **IPC / Sockets** | `ipc.c`, `socket.c`, `apps.c` — inter-process communication primitives |

---

## 🏗️ Architecture

### Boot sequence

```
boot/boot.asm                 (Multiboot2 entry — 32-bit protected mode)
    │
    ├─ CPU feature detection (CPUID, long mode check)
    ├─ GDT setup + identity paging (2MB huge pages)
    │
    ▼
boot/long_mode.asm            (64-bit jump target)
    │
    ▼
kernel/main.c                 (C kernel entry)
    ├─ PMM → VMM → IDT/IRQ init
    ├─ PIT timer + PS/2 keyboard
    ├─ VGA / framebuffer init
    ├─ VFS + FAT32 mount
    ├─ ELF loader (sys_exec)
    ├─ Scheduler + task creation
    └─ Shell launch
```

### Kernel components

| File | Responsibility |
|---|---|
| `kernel/memory.c` | PMM bitmap allocator, best-fit heap, VMM |
| `kernel/task.c` | PCB, 8KB stacks, guard pages, stack canaries |
| `kernel/scheduler.c` | Round-robin preemptive scheduler, yield |
| `kernel/idt.c` + `irq.c` | 256-entry IDT, PIC remapping to IRQ 32–47 |
| `kernel/timer.c` | PIT driver — 1000 Hz system tick |
| `kernel/keyboard.c` | PS/2 scan code → ASCII translation |
| `kernel/vga.c` | 80×25 VGA text mode + hardware cursor |
| `kernel/shell.c` | Built-in command interpreter |
| `kernel/net.c` | RTL8139 Ethernet driver |
| `kernel/wm.c` + `desktop.c` | Framebuffer window manager + desktop |
| `kernel/il_runtime.c` | CIL/IL bytecode interpreter |
| `kernel/ipc.c` + `socket.c` | IPC primitives + socket layer |
| `kernel/audio.c` + `usb.c` | Audio and USB subsystems (in progress) |
| `kernel/pci.c` + `power.c` | PCI enumeration + power management |

---

## 📁 Repository structure

```
charis__OS/
├── boot/                    # Bootloader assembly
│   ├── boot.asm             # 32-bit entry, mode transitions
│   └── long_mode.asm        # 64-bit jump target
├── kernel/                  # C kernel source
│   ├── main.c               # Kernel entry point
│   ├── memory.c             # PMM + VMM + heap allocator
│   ├── task.c               # Task creation and PCB
│   ├── scheduler.c          # Preemptive round-robin scheduler
│   ├── idt.c / irq.c        # IDT (256 entries) + PIC remapping
│   ├── timer.c              # PIT driver, 1000 Hz
│   ├── keyboard.c           # PS/2 keyboard driver
│   ├── vga.c                # 80×25 VGA text mode + cursor
│   ├── shell.c              # Built-in command interpreter
│   ├── net.c                # RTL8139 Ethernet driver
│   ├── wm.c / desktop.c     # Framebuffer WM + desktop
│   ├── il_runtime.c         # CIL/IL bytecode interpreter
│   ├── ipc.c / socket.c     # IPC primitives + socket layer
│   ├── audio.c / usb.c      # Audio + USB subsystems
│   ├── pci.c / power.c      # PCI enumeration + power mgmt
│   └── asm/                 # Assembly helpers
├── include/kernel/          # Kernel headers
├── iso/boot/grub/           # GRUB2 config for ISO image
├── link.ld                  # Custom GNU ld linker script
├── Makefile                 # Main build (freestanding GCC)
├── Makefile.bak             # Backup Makefile
├── build_wsl.sh             # WSL build helper (Linux)
├── build_wsl.bat            # WSL build wrapper (Windows)
├── build.bat                # Windows batch build
├── run.bat                  # QEMU launch (Windows)
├── OPTIMIZATIONS.md         # Performance notes
├── TODO.md                  # Backlog and task list
└── SECURITY.md              # Security policy
```

---

## 🛠️ Building & Running

### Prerequisites

```bash
# Debian / Ubuntu / WSL
sudo apt update
sudo apt install build-essential nasm grub-pc-bin xorriso qemu-system-x86

# Freestanding cross-compiler (if not already available)
sudo apt install gcc binutils
```

### Build & run (Linux / WSL)

```bash
# Clone
git clone https://github.com/Liladizilla/charis__OS.git
cd charis__OS

# Build the bootable ISO
make

# Run in QEMU (256MB RAM, serial output to stdout)
qemu-system-x86_64 -cdrom build/charisOS.iso -m 256M -serial stdio
```

### Windows (WSL wrapper)

```bat
:: Build via WSL from cmd.exe
build_wsl.bat

:: Launch QEMU
run.bat
```

> **Note:** WSL availability depends on your machine configuration. If WSL is not available, use a native Linux environment or a Docker container with the cross-compiler toolchain.

### QEMU flags reference

| Flag | Purpose |
|---|---|
| `-m 256M` | Allocate 256MB RAM (minimum 64MB) |
| `-serial stdio` | Route serial debug output to terminal |
| `-display gtk` | GUI window (default) |
| `-nographic` | Headless mode — serial only |
| `-d int` | Log CPU interrupts (debug builds) |

---

## 📊 Technical specifications

| Component | Implementation |
|---|---|
| Architecture | x86_64 (64-bit, AMD64 / Intel 64) |
| Boot standard | Multiboot2 via GRUB2 |
| Kernel language | C — freestanding GCC, no stdlib |
| Boot language | NASM Assembly |
| Memory model | Flat + paging (2MB huge pages, identity mapped) |
| Max tasks | 32 concurrent tasks × 8KB stack each |
| Timer frequency | 1000 Hz (configurable PIT) |
| Syscall dispatch | SYSCALL/SYSRET + MSR setup |
| Linker | GNU ld with custom `link.ld` script |
| Test environment | QEMU 7+ and HP laptop (bare metal) |
| Minimum RAM | 64MB (tested with 256MB) |

---

## 🗺️ Roadmap

- [x] **Phase 1** — Boot: Real → Protected → Long mode, GDT, paging
- [x] **Phase 2** — Core kernel: PMM, heap allocator, IDT, IRQ, PIT
- [x] **Phase 3** — Drivers: VGA, PS/2 keyboard, serial debug
- [x] **Phase 4** — Multitasking: PCB, preemptive scheduler, stack canaries
- [x] **Phase 5** — Storage + ELF: VFS, FAT32, `sys_exec()`, framebuffer + PSF2
- [ ] **Phase 6** — Userspace: Ring 3 / TSS, full SYSCALL ABI, CIL VM hardening
- [ ] USB HID + basic PCM audio output
- [ ] TCP/IP stack on RTL8139 Ethernet
- [ ] Window manager compositing + app launcher
- [ ] `socket.c` stability (prevent premature kernel panics)

---

## 🔢 Language breakdown

```
C              ████████████████████████░  74.0%
Assembly       ████░                       11.6%
Batchfile      ██░                          7.3%
Shell          █░                           3.8%
Makefile       ░                            2.3%
Linker Script  ░                            0.7%
PowerShell     ░                            0.3%
```

---

## 📚 References & Learning Resources

| Resource | Topic |
|---|---|
| [The Little OS Book](https://littleosbook.github.io) | OS dev foundations |
| [OSDev Wiki](https://wiki.osdev.org) | x86 hardware reference |
| [James Molloy's Kernel Tutorial](https://jamesmolloy.co.uk) | C kernel walkthrough |
| [Tuhdo OS Tutorial](https://tuhdo.github.io/os01) | x86_64 systems |
| [os.phil-opp.com](https://os.phil-opp.com) | Writing an OS in Rust |
| [Arjun Sreedharan — Kernel 101](https://arjunsreedharan.org) | Linux kernel internals |
| [Linux From Scratch](https://linuxfromscratch.org) | Building a complete OS |
| [Beej's Guide to Network Programming](https://beej.us/guide/bgnet) | Networking / sockets |
| [Writing a Simple TCP/IP Stack](https://saminiir.com/lets-code-tcp-ip-stack) | Network stack |
| [LC-3 VM Tutorial](https://justinmeiners.github.io/lc3-vm) | Bytecode VM / IL runtime |
| [Dan Luu — malloc tutorial](https://danluu.com/malloc-tutorial) | Memory allocator design |
| [Crafting Interpreters](https://craftinginterpreters.com) | Language + bytecode VM |

---

## 🤝 Contributing

Contributions, bug reports, and ideas are welcome.

1. Fork the repository
2. Create a feature branch: `git checkout -b feat/your-feature`
3. Commit your changes: `git commit -m "feat: describe your change"`
4. Push and open a Pull Request

Please read [`SECURITY.md`](./SECURITY.md) before reporting security-sensitive issues.

---

## 👤 Author

**Charis Chara**  


- GitHub: [@Liladizilla](https://github.com/Liladizilla)
- Portfolio: [portfolio-self-five-47.vercel.app](https://portfolio-self-five-47.vercel.app)
- Personal site: [cyberzilla01.pp.ua](https://cyberzilla01.pp.ua)

---

## 📄 License

This project is licensed under the [MIT License](./LICENSE).

---

<div align="center">
  <sub>Built with ⚙️ from scratch — no OS, no stdlib, no shortcuts.</sub>
</div>
