# CharisOS Architecture Map

## Runtime path

```text
GRUB2 / Multiboot2
  -> boot/boot.asm (32-bit entry, stack, CPU checks, paging)
  -> boot/long_mode.asm (64-bit segments and C ABI handoff)
  -> kernel/main.c
       -> memory and VMM
       -> IDT, PIC, PIT, keyboard
       -> storage, FAT/VFS
       -> tasks, scheduler, syscalls
       -> graphics, window manager, desktop
       -> services, diagnostics, security
       -> user and shell tasks
```

## Ownership boundaries

- `boot/`: assembly required before C is available.
- `kernel/asm/`: context switching, I/O, interrupt entry, and GDT helpers.
- `kernel/`: freestanding C services and drivers.
- `include/kernel/`: interfaces between kernel subsystems.
- `link.ld`: physical load layout and section boundaries.
- `iso/`: GRUB boot media configuration.

## Stabilization rules

1. Bring up serial and VGA before optional hardware.
2. Validate Multiboot data before using it.
3. Keep each driver optional and failure-safe.
4. A missing device must not prevent text-mode boot.
5. Test memory, interrupts, timer, keyboard, and scheduler before desktop features.
6. Keep the kernel freestanding: C plus NASM assembly, with no Node.js, TypeScript, libc, or hosted runtime.

## Known work areas

- Verify the cross-compiler and complete Makefile dependency graph.
- Parse Multiboot2 framebuffer tags before enabling framebuffer graphics.
- Replace simulated or hard-coded device paths with validated PCI discovery.
- Add QEMU smoke tests and panic/serial diagnostics for every initialization stage.
